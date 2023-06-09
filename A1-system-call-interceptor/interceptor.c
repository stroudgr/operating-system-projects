#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <asm/current.h>
#include <asm/ptrace.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <asm/unistd.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/syscalls.h>
#include "interceptor.h"


MODULE_DESCRIPTION("My kernel module");
MODULE_AUTHOR("Graeme Stroud");
MODULE_LICENSE("GPL");

//----- My function declarations ------------------------------------
int monitor_allowed(int pid);
int request_intercept(int syscall, int pid);
int request_release(int syscall, int pid);
int request_start_monitor(int syscall, int pid);
int request_stop_monitor(int syscall, int pid);

//-------------------------------------------------------------


//----- System Call Table Stuff ------------------------------------
/* Symbol that allows access to the kernel system call table */
extern void* sys_call_table[];

/* The sys_call_table is read-only => must make it RW before replacing a syscall */
void set_addr_rw(unsigned long addr) {

	unsigned int level;
	pte_t *pte = lookup_address(addr, &level);

	if (pte->pte &~ _PAGE_RW) pte->pte |= _PAGE_RW;

}

/* Restores the sys_call_table as read-only */
void set_addr_ro(unsigned long addr) {

	unsigned int level;
	pte_t *pte = lookup_address(addr, &level);

	pte->pte = pte->pte &~_PAGE_RW;

}
//-------------------------------------------------------------


//----- Data structures and bookkeeping -----------------------
/**
 * This block contains the data structures needed for keeping track of
 * intercepted system calls (including their original calls), pid monitoring
 * synchronization on shared data, etc.
 * It's highly unlikely that you will need any globals other than these.
 */

/* List structure - each intercepted syscall may have a list of monitored pids */
struct pid_list {
	pid_t pid;
	struct list_head list;
};


/* Store info about intercepted/replaced system calls */
typedef struct {

	/* Original system call */
	asmlinkage long (*f)(struct pt_regs);

	/* Status: 1=intercepted, 0=not intercepted */
	int intercepted;

	/* Are any PIDs being monitored for this syscall? */
	int monitored;
	/* List of monitored PIDs */
	int listcount;
	struct list_head my_list;
}mytable;

/* An entry for each system call in this "metadata" table */
mytable table[NR_syscalls];

/* Access to the system call table and your metadata table must be synchronized */
spinlock_t my_table_lock = SPIN_LOCK_UNLOCKED;
spinlock_t sys_call_table_lock = SPIN_LOCK_UNLOCKED;
//-------------------------------------------------------------


//----------LIST OPERATIONS------------------------------------
/**
 * These operations are meant for manipulating the list of pids
 * Nothing to do here, but please make sure to read over these functions
 * to understand their purpose, as you will need to use them!
 */

/**
 * Add a pid to a syscall's list of monitored pids.
 * Returns -ENOMEM if the operation is unsuccessful.
 */
static int add_pid_sysc(pid_t pid, int sysc)
{
	struct pid_list *ple=(struct pid_list*)kmalloc(sizeof(struct pid_list), GFP_KERNEL);

	if (!ple)
		return -ENOMEM;

	INIT_LIST_HEAD(&ple->list);
	ple->pid=pid;

	list_add(&ple->list, &(table[sysc].my_list));
	table[sysc].listcount++;

	return 0;
}

/**
 * Remove a pid from a system call's list of monitored pids.
 * Returns -EINVAL if no such pid was found in the list.
 */
static int del_pid_sysc(pid_t pid, int sysc)
{
	struct list_head *i;
	struct pid_list *ple;

	list_for_each(i, &(table[sysc].my_list)) {

		ple=list_entry(i, struct pid_list, list);
		if(ple->pid == pid) {

			list_del(i);
			kfree(ple);

			table[sysc].listcount--;
			/* If there are no more pids in sysc's list of pids, then
			 * stop the monitoring only if it's not for all pids (monitored=2) */
			if(table[sysc].listcount == 0 && table[sysc].monitored == 1) {
				table[sysc].monitored = 0;
			}

			return 0;
		}
	}

	return -EINVAL;
}

/**
 * Remove a pid from all the lists of monitored pids (for all intercepted syscalls).
 * Returns -1 if this process is not being monitored in any list.
 */
static int del_pid(pid_t pid)
{
	struct list_head *i, *n;
	struct pid_list *ple;
	int ispid = 0, s = 0;

	for(s = 1; s < NR_syscalls; s++) {

		list_for_each_safe(i, n, &(table[s].my_list)) {

			ple=list_entry(i, struct pid_list, list);
			if(ple->pid == pid) {

				list_del(i);
				ispid = 1;
				kfree(ple);

				table[s].listcount--;
				/* If there are no more pids in sysc's list of pids, then
				 * stop the monitoring only if it's not for all pids (monitored=2) */
				if(table[s].listcount == 0 && table[s].monitored == 1) {
					table[s].monitored = 0;
				}
			}
		}
	}

	if (ispid) return 0;
	return -1;
}

/**
 * Clear the list of monitored pids for a specific syscall.
 */
static void destroy_list(int sysc) {

	struct list_head *i, *n;
	struct pid_list *ple;

	list_for_each_safe(i, n, &(table[sysc].my_list)) {

		ple=list_entry(i, struct pid_list, list);
		list_del(i);
		kfree(ple);
	}

	table[sysc].listcount = 0;
	table[sysc].monitored = 0;
}

/**
 * Check if two pids have the same owner - useful for checking if a pid
 * requested to be monitored is owned by the requesting process.
 * Remember that when requesting to start monitoring for a pid, only the
 * owner of that pid is allowed to request that.
 */
static int check_pids_same_owner(pid_t pid1, pid_t pid2) {

	struct task_struct *p1 = pid_task(find_vpid(pid1), PIDTYPE_PID);
	struct task_struct *p2 = pid_task(find_vpid(pid2), PIDTYPE_PID);
	if(p1->real_cred->uid != p2->real_cred->uid)
		return -EPERM;
	return 0;
}

/**
 * Check if a pid is already being monitored for a specific syscall.
 * Returns 1 if it already is, or 0 if pid is not in sysc's list.
 */
static int check_pid_monitored(int sysc, pid_t pid) {

	struct list_head *i;
	struct pid_list *ple;

	list_for_each(i, &(table[sysc].my_list)) {

		ple=list_entry(i, struct pid_list, list);
		if(ple->pid == pid)
			return 1;

	}
	return 0;
}
//----------------------------------------------------------------

//----- Intercepting exit_group ----------------------------------
/**
 * Since a process can exit without its owner specifically requesting
 * to stop monitoring it, we must intercept the exit_group system call
 * so that we can remove the exiting process's pid from *all* syscall lists.
 */

/**
 * Stores original exit_group function - after all, we must restore it
 * when our kernel module exits.
 */
void (*orig_exit_group)(int);

/**
 * Our custom exit_group system call.
 *
 * TODO: When a process exits, make sure to remove that pid from all lists.
 * The exiting process's PID can be retrieved using the current variable (current->pid).
 * Don't forget to call the original exit_group.
 *
 * Note: using printk in this function will potentially result in errors!
 *
 */
void my_exit_group(int status)
{
	spin_lock(&my_table_lock);
	// Gets deleted from every system call's list.
	del_pid(current->pid);
	spin_unlock(&my_table_lock);
	orig_exit_group(status);
}
//----------------------------------------------------------------



/**
 * This is the generic interceptor function.
 * It should just log a message and call the original syscall.
 *
 * TODO: Implement this function.
 * - Check first to see if the syscall is being monitored for the current->pid.
 * - Recall the convention for the "monitored" flag in the mytable struct:
 *     monitored=0 => not monitored
 *     monitored=1 => some pids are monitored, check the corresponding my_list
 *     monitored=2 => all pids are monitored for this syscall
 * - Use the log_message macro, to log the system call parameters!
 *     Remember that the parameters are passed in the pt_regs registers.
 *     The syscall parameters are found (in order) in the
 *     ax, bx, cx, dx, si, di, and bp registers (see the pt_regs struct).
 * - Don't forget to call the original system call, so we allow processes to proceed as normal.
 *
 */
asmlinkage long interceptor(struct pt_regs reg) {

	// Note that syscall = reg.ax.

	// Prints a log message if this gets set to 1.
	int log_flag = 0;
	spin_lock(&my_table_lock);
		// Unintercepted calls ignore this (just call the original system call).
		if (table[reg.ax].intercepted){
			// Is this pid monitored in a list?
			if (table[reg.ax].monitored == 1 && check_pid_monitored(reg.ax, current->pid)){
				log_flag = 1;
			}
			// Is this pid monitored because all are monitored?
			if (table[reg.ax].monitored == 2){
				log_flag = 1;
			}
		}
	spin_unlock(&my_table_lock);

	if (log_flag){
		log_message(current->pid, reg.ax, reg.bx, reg.cx, reg.dx, reg.si, reg.di, reg.bp);
	}

/* ANNOTATION 1: Unsafe access ( synch -2) */
	return table[reg.ax].f(reg);
/* END ANNOTATION 1 */

}

/**
 * My system call - this function is called whenever a user issues a MY_CUSTOM_SYSCALL system call.
 * When that happens, the parameters for this system call indicate one of 4 actions/commands:
 *      - REQUEST_SYSCALL_INTERCEPT to intercept the 'syscall' argument
 *      - REQUEST_SYSCALL_RELEASE to de-intercept the 'syscall' argument
 *      - REQUEST_START_MONITORING to start monitoring for 'pid' whenever it issues 'syscall'
 *      - REQUEST_STOP_MONITORING to stop monitoring for 'pid'
 *      For the last two, if pid=0, that translates to "all pids".
 *
 * TODO: Implement this function, to handle all 4 commands correctly.
 *
 * - For each of the commands, check that the arguments are valid (-EINVAL):
 *   a) the syscall must be valid (not negative, not > NR_syscalls, and not MY_CUSTOM_SYSCALL itself)
 *   b) the pid must be valid for the last two commands. It cannot be a negative integer,
 *      and it must be an existing pid (except for the case when it's 0, indicating that we want
 *      to start/stop monitoring for "all pids").
 *      If a pid belongs to a valid process, then the following expression is non-NULL:
 *           pid_task(find_vpid(pid), PIDTYPE_PID)
 * - Check that the caller has the right permissions (-EPERM)
 *      For the first two commands, we must be root (see the current_uid() macro).
 *      For the last two commands, the following logic applies:
 *        - is the calling process root? if so, all is good, no doubts about permissions.
 *        - if not, then check if the 'pid' requested is owned by the calling process
 *        - also, if 'pid' is 0 and the calling process is not root, then access is denied
 *          (monitoring all pids is allowed only for root, obviously).
 *      To determine if two pids have the same owner, use the helper function provided above in this file.
 * - Check for correct context of commands (-EINVAL):
 *     a) Cannot de-intercept a system call that has not been intercepted yet.
 *     b) Cannot stop monitoring for a pid that is not being monitored, or if the
 *        system call has not been intercepted yet.
 * - Check for -EBUSY conditions:
 *     a) If intercepting a system call that is already intercepted.
 *     b) If monitoring a pid that is already being monitored.
 * - If a pid cannot be added to a monitored list, due to no memory being available,
 *   an -ENOMEM error code should be returned.
 *
 *   NOTE: The order of the checks may affect the tester, in case of several error conditions
 *   in the same system call, so please be careful!
 *
 * - Make sure to keep track of all the metadata on what is being intercepted and monitored.
 *   Use the helper functions provided above for dealing with list operations.
 *
 * - Whenever altering the sys_call_table, make sure to use the set_addr_rw/set_addr_ro functions
 *   to make the system call table writable, then set it back to read-only.
 *   For example: set_addr_rw((unsigned long)sys_call_table);
 *   Also, make sure to save the original system call (you'll need it for 'interceptor' to work correctly).
 *
 * - Make sure to use synchronization to ensure consistency of shared data structures.
 *   Use the calltable_spinlock and pidlist_spinlock to ensure mutual exclusion for accesses
 *   to the system call table and the lists of monitored pids. Be careful to unlock any spinlocks
 *   you might be holding, before you exit the function (including error cases!).
 */
asmlinkage long my_syscall(int cmd, int syscall, int pid) {
	int result;

	// The system call number must be valid.
	if (syscall > NR_syscalls - 1 || syscall < 0 || syscall == MY_CUSTOM_SYSCALL)
		return -EINVAL;
	// Pid check is below in  request_start_monitor and request_stop_monitor

	switch (cmd) {
		case REQUEST_SYSCALL_INTERCEPT:
			// Implementation of this function (and the other 3) are below.
			result = request_intercept(syscall, pid);
			break;

		case REQUEST_SYSCALL_RELEASE:
			result = request_release(syscall, pid);
			break;

		case REQUEST_START_MONITORING:
			result = request_start_monitor(syscall, pid);
			break;

		case REQUEST_STOP_MONITORING:
			result = request_stop_monitor(syscall, pid);
			break;

		default:
			// Not a valid command.
			result = -EINVAL;
			break;
	}

	return result;
}

/*
* Given the system call number syscall, this function will "intercept" the
* corresponding system call. This means the behaviour of the system call will
* change to include a logging message (see interceptor) if a monitor request
* has beeen made (See request_start_monitor/request_stop_monitor below).
*/
int request_intercept(int syscall, int pid){

	// What to return.
	int flag = 0;

	// Only root can intercept
	if (current_uid() != 0){
		return -EPERM;
	}

	spin_lock(&my_table_lock);
		// Don't intercept if already intercepted.
		if (table[syscall].intercepted == 1)
			flag = -EBUSY;
		else{
			table[syscall].intercepted = 1;
			//Replaces the system call with the interceptor.
      spin_lock(&sys_call_table_lock);
			set_addr_rw((unsigned long)sys_call_table);
			sys_call_table[syscall] = interceptor;
			set_addr_ro((unsigned long)sys_call_table);
      spin_unlock(&sys_call_table_lock);
		}
	spin_unlock(&my_table_lock);

	if (flag){
		return -EBUSY;
	}

	return 0;
}

/*
* Given the system call number syscall, this function will "deintercept" the
* corresponding system call. The behaviour of the system call syscal will
* return to the original behaviour before a call to request_intercept was made.
* Note that request_release should only be called if the request_intercept was
* called for this system call, an error will be returned in this case.
*/
int request_release(int syscall, int pid){

	int flag = 0;

	// Only root can deintercept
	if (current_uid() != 0){
		return -EPERM;
	}

	spin_lock(&my_table_lock);
		// Don't de-intercept if not intercepted yet
		if (table[syscall].intercepted == 0)
			flag = -EINVAL;
		else{
			table[syscall].intercepted = 0;
			table[syscall].monitored = 0;
      spin_lock(&sys_call_table_lock);
				set_addr_rw((unsigned long)sys_call_table);
				// Changes system call from interceptor back to original.
				sys_call_table[syscall] = table[syscall].f;
				set_addr_ro((unsigned long)sys_call_table);
        spin_unlock(&sys_call_table_lock);
			destroy_list(syscall);
		}
	spin_unlock(&my_table_lock);

	if (flag){
		return flag;
	}

	return 0;
}

/**
 * Given a syscall and a pid, the system call will request to start monitoring
 * this pid. This means the system call will make a logging command for any
 * pid that is the pid parameter for start_monitoring. This can be undone using
 * request_stop_monitor (see below). In the case pid = 0, the system call will
 * monitor all pids.
 */
int request_start_monitor(int syscall, int pid){

	// Checks basic permissions. See implementation below.
	int flag = monitor_allowed(pid);
	if (flag)
		return flag;
	flag = 0;

	spin_lock(&my_table_lock);
		// The pid must be a valid pid or 0.
		// This is here in the critical section (as opposed to happening right
		//  before) because a proccess may exit before this lock, and then
		//   pid_task(find_vpid(pid), PIDTYPE_PID) could've become NULL after
		//    this check.
		if (pid != 0 && (pid_task(find_vpid(pid), PIDTYPE_PID) == NULL || pid < 0)){
			flag = -EINVAL;
		}
		// Cannot monitor a pid for a syscall that yet to be intercepted.
		else if (table[syscall].intercepted == 0)
/* ANNOTATION 4: Better coding style would be to unlock and return within error block, then do it later based on a flag value (-2) */
			flag = -EINVAL;
/* END ANNOTATION 4 */
		else {

			int m = table[syscall].monitored;
			int val_ret = check_pid_monitored(syscall, pid);
			// Is it already monitored? Two cases:
			// Requesting to monitor proccess(es), but all proccesses are being
			// monitored    OR    this pid is already in the list.
			if (m == 2 || (m == 1 && val_ret))
				flag = -EBUSY;

			else if (pid == 0){
				table[syscall].monitored = 2;
				//destroy_list(syscall); NOTE: I think this is what should happen, but
				// this causes one of the test_full tests to fail.
			}

			// Else, monitor a specific, unmonitored process
			else {
				// Try to add pid to this syscall's list of monitored pids.
				table[syscall].monitored = 1;
				if (add_pid_sysc(pid, syscall) == -ENOMEM){
					flag = -ENOMEM;
					table[syscall].monitored = m;
				}
			}

		}
	spin_unlock(&my_table_lock);

	if (flag){
		return flag;
	}

	return 0;

}

/**
 * Given a syscall and a pid, the system call will request to stop monitoring
 * this pid. This means the system call will not make a logging command for a
 * pid after call start_monitoring with parameter pid. In the case pid = 0, the
 * system call will stop monitoring all pids for the syscall. Error is returned
 * Id trying to stop monitoring something that is not being monitored.
 */
int request_stop_monitor(int syscall, int pid){

	int flag = monitor_allowed(pid);
	if (flag)
		return flag;
	flag = 0;

/* ANNOTATION 5: Indentation problem (-2) */
	spin_lock(&my_table_lock);

		if (pid != 0 && (pid_task(find_vpid(pid), PIDTYPE_PID) == NULL || pid < 0)){
			flag = -EINVAL;
		}
/* END ANNOTATION 5 */

		else {
			// Cannot stop monitoring if haven't started.
			if (table[syscall].monitored == 0 || table[syscall].intercepted == 0)
				flag = -EINVAL;

			// Stop monitoring all proccesses
/* ANNOTATION 3: No call to destroy_list (-2) */
			else if (pid == 0){
/* END ANNOTATION 3 */
				if (table[syscall].listcount > 0) //Shouldn't happen.
					table[syscall].monitored = 1;
				else
					table[syscall].monitored = 0;
			}
			// Remove one pid from list
			else {
				if (table[syscall].monitored == 2)
					flag = -EINVAL; // BONUS case (I didn't bother)
				else
					// This will also set table[syscall].monitored appropriatly.
					del_pid_sysc(pid, syscall);
			}
		}

	spin_unlock(&my_table_lock);
	if (flag)
		return flag;

	return 0;
}


/**
* Returns whether this process is allowed to monitor the given proccess (pid).
* 0 if so, -EPERM o/w.
*/
int monitor_allowed(int pid){

	int r;

	//root? Then yes.
	if (current_uid() == 0)
		return 0;

	// Else, only root can monitor all proccesses.
	if (pid == 0){
		return -EPERM;
	}
	// If pid is owned by this proccess, then allow.
	r = check_pids_same_owner(current->pid, pid);

	return r;

}


/**
 * Saving the original system call that is being hijacked with my_syscall.
 */
long (*orig_custom_syscall)(void);


/**
 * Module initialization.
 *
 * TODO: Make sure to:
 * - Hijack MY_CUSTOM_SYSCALL and save the original in orig_custom_syscall.
 * - Hijack the exit_group system call (__NR_exit_group) and save the original
 *   in orig_exit_group.
 * - Make sure to set the system call table to writable when making changes,
 *   then set it back to read only once done.
 * - Perform any necessary initializations for bookkeeping data structures.
 *   To initialize a list, use
 *        INIT_LIST_HEAD (&some_list);
 *   where some_list is a "struct list_head".
 * - Ensure synchronization as needed.
 */
static int init_function(void) {

	int s;

  // Initialize each table entry for each system call.
  spin_lock(&my_table_lock);
	spin_lock(&sys_call_table_lock);
	 for (s = 1; s < NR_syscalls; s++){
			table[s].intercepted = 0;
			table[s].monitored = 0;
			table[s].listcount = 0;
			INIT_LIST_HEAD(&(table[s].my_list));
			// NOTE: Save the original system call.
			if (s == __NR_exit_group)
				table[s].f = (asmlinkage long (*)(struct pt_regs)) my_exit_group;
			else
/* ANNOTATION 2: Saving every system call in table on initialization of module (not on actual intercept of system call) (-2) */
				table[s].f = sys_call_table[s];
/* END ANNOTATION 2 */

		}
	spin_unlock(&sys_call_table_lock);
	spin_unlock(&my_table_lock);

	// Hijack sys_call_table with my system call, and a more appropriate exit
  // group function.
  spin_lock(&sys_call_table_lock);
    orig_custom_syscall = sys_call_table[MY_CUSTOM_SYSCALL];
    orig_exit_group = sys_call_table[__NR_exit_group];
    set_addr_rw((unsigned long)sys_call_table);
    sys_call_table[MY_CUSTOM_SYSCALL] = my_syscall;
    sys_call_table[__NR_exit_group] = my_exit_group;
    set_addr_ro((unsigned long)sys_call_table);
  spin_unlock(&sys_call_table_lock);


	return 0;
}

/**
 * Module exits.
 *
 * TODO: Make sure to:
 * - Restore MY_CUSTOM_SYSCALL to the original syscall.
 * - Restore __NR_exit_group to its original syscall.
 * - Make sure to set the system call table to writable when making changes,
 *   then set it back to read only once done.
 * - Make sure to deintercept all syscalls, and cleanup all pid lists.
 * - Ensure synchronization, if needed.
 */
static void exit_function(void)
{

	int s;

	spin_lock(&my_table_lock);
	spin_lock(&sys_call_table_lock);
  	for (s = 1; s < NR_syscalls; s++){

  		set_addr_rw((unsigned long)sys_call_table);
  		if (s == __NR_exit_group)
  			sys_call_table[__NR_exit_group] = orig_exit_group;
  		else
  			sys_call_table[s] = table[s].f;
  		set_addr_ro((unsigned long)sys_call_table);

      destroy_list(s);
  		if (table[s].intercepted){
  			table[s].intercepted = 0;
  		}
  		table[s].monitored = 0;
  		table[s].listcount = 0;
  	}
	spin_unlock(&sys_call_table_lock);
  spin_unlock(&my_table_lock);

  spin_lock(&sys_call_table_lock);
	  set_addr_ro((unsigned long)sys_call_table);
	  sys_call_table[MY_CUSTOM_SYSCALL] = orig_custom_syscall;
	  set_addr_ro((unsigned long)sys_call_table);
	spin_unlock(&sys_call_table_lock);

}

module_init(init_function);
module_exit(exit_function);
