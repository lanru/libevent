/*
 * Copyright (c) 2000-2007 Niels Provos <provos@citi.umich.edu>
 * Copyright (c) 2007-2012 Niels Provos and Nick Mathewson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef EVENT_INTERNAL_H_INCLUDED_
#define EVENT_INTERNAL_H_INCLUDED_

#ifdef __cplusplus
extern "C" {
#endif

#include "event2/event-config.h"
#include "event2/watch.h"
#include "evconfig-private.h"

#include <time.h>
#include <sys/queue.h>
#include "event2/event_struct.h"
#include "minheap-internal.h"
#include "evsignal-internal.h"
#include "mm-internal.h"
#include "defer-internal.h"

/* map union members back */

/* mutually exclusive */
#define ev_signal_next	ev_.ev_signal.ev_signal_next
#define ev_io_next	ev_.ev_io.ev_io_next
#define ev_io_timeout	ev_.ev_io.ev_timeout

/* used only by signals */
#define ev_ncalls	ev_.ev_signal.ev_ncalls
#define ev_pncalls	ev_.ev_signal.ev_pncalls

#define ev_pri ev_evcallback.evcb_pri
#define ev_flags ev_evcallback.evcb_flags
#define ev_closure ev_evcallback.evcb_closure
#define ev_callback ev_evcallback.evcb_cb_union.evcb_callback
#define ev_arg ev_evcallback.evcb_arg

/** @name Event closure codes

    Possible values for evcb_closure in struct event_callback

    @{
 */
/** A regular event. Uses the evcb_callback callback */
#define EV_CLOSURE_EVENT 0
/** A signal event. Uses the evcb_callback callback */
#define EV_CLOSURE_EVENT_SIGNAL 1
/** A persistent non-signal event. Uses the evcb_callback callback */
#define EV_CLOSURE_EVENT_PERSIST 2
/** A simple callback. Uses the evcb_selfcb callback. */
#define EV_CLOSURE_CB_SELF 3
/** A finalizing callback. Uses the evcb_cbfinalize callback. */
#define EV_CLOSURE_CB_FINALIZE 4
/** A finalizing event. Uses the evcb_evfinalize callback. */
#define EV_CLOSURE_EVENT_FINALIZE 5
/** A finalizing event that should get freed after. Uses the evcb_evfinalize
 * callback. */
#define EV_CLOSURE_EVENT_FINALIZE_FREE 6
/** @} */

/** Structure to define the backend of a given event_base. */
struct eventop {
	/** The name of this backend.
	 * 后端I/O复用技术的名称. */
	const char *name;
	/** Function to set up an event_base to use this backend.  It should
	 * create a new structure holding whatever information is needed to
	 * run the backend, and return it.  The returned pointer will get
	 * stored by event_init into the event_base.evbase field.  On failure,
	 * this function should return NULL.
	 * 初始化函数. */
	void *(*init)(struct event_base *);
	/** Enable reading/writing on a given fd or signal.  'events' will be
	 * the events that we're trying to enable: one or more of EV_READ,
	 * EV_WRITE, EV_SIGNAL, and EV_ET.  'old' will be those events that
	 * were enabled on this fd previously.  'fdinfo' will be a structure
	 * associated with the fd by the evmap; its size is defined by the
	 * fdinfo field below.  It will be set to 0 the first time the fd is
	 * added.  The function should return 0 on success and -1 on error.
	 * 注册事件.
	 */
	int (*add)(struct event_base *, evutil_socket_t fd, short old, short events, void *fdinfo);
	/** As "add", except 'events' contains the events we mean to disable.
	 * 删除事件. */
	int (*del)(struct event_base *, evutil_socket_t fd, short old, short events, void *fdinfo);
	/** Function to implement the core of an event loop.  It must see which
	    added events are ready, and cause event_active to be called for each
	    active event (usually via event_io_active or such).  It should
	    return 0 on success and -1 on error.
	 等待事件.
	 */
	int (*dispatch)(struct event_base *, struct timeval *);
	/** Function to clean up and free our data from the event_base.
	 * 释放I/O复用机制使用的资源*/
	void (*dealloc)(struct event_base *);
	/** Flag: set if we need to reinitialize the event base after we fork.
	 程序调用fork之后是否需要重新初始化event_base.
	 */
	int need_reinit;
	/** Bit-array of supported event_method_features that this backend can
	 * provide.
	 * I/O复用技术支持的一些特性,可选如下3个值的按位或:EV_FEATURE_ET(支持边沿触发事
	 * 件EV_ET)、EV_FEATURE_01(事件检测算法的复杂度是0(1))和EV_FEATURE_FDS(不仅
	 * 能监听socket上的事件,还能监听其他类型的文件描述符上的事件).  */
	enum event_method_feature features;
	/** Length of the extra information we should record for each fd that
	    has one or more active events.  This information is recorded
	    as part of the evmap entry for each fd, and passed as an argument
	    to the add and del functions above.

	    有的IO复用机制需要为每个I/O事件队列和信号事件队列分配额外的内存,以避免同一个文
	    件描述符被重复插入I/O复用机制的事件表中。evmap_io_add(evmap_io_del)函数在
	    调用eventop的add(或del)方法时,将这段内存的起始地址作为第5个参数传递给add(或
	    de1)方法。下面这个成员则指定了这段内存的长度.
	 */
	size_t fdinfo_len;
};

#ifdef _WIN32
/* If we're on win32, then file descriptors are not nice low densely packed
   integers.  Instead, they are pointer-like windows handles, and we want to
   use a hashtable instead of an array to map fds to events.
*/
#define EVMAP_USE_HT
#endif

/* #define HT_CACHE_HASH_VALS */

#ifdef EVMAP_USE_HT
#define HT_NO_CACHE_HASH_VALUES
#include "ht-internal.h"
struct event_map_entry;
HT_HEAD(event_io_map, event_map_entry);
#else
#define event_io_map event_signal_map
#endif

/* Used to map signal numbers to a list of events.  If EVMAP_USE_HT is not
   defined, this structure is also used as event_io_map, which maps fds to a
   list of events.
*/
struct event_signal_map {
	/* An array of evmap_io * or of evmap_signal *; empty entries are
	 * set to NULL. */
	void **entries;
	/* The number of entries available in entries */
	int nentries;
};

/* A list of events waiting on a given 'common' timeout value.  Ordinarily,
 * events waiting for a timeout wait on a minheap.  Sometimes, however, a
 * queue can be faster.
 **/
struct common_timeout_list {
	/* List of events currently waiting in the queue. */
	struct event_list events;
	/* 'magic' timeval used to indicate the duration of events in this
	 * queue. */
	struct timeval duration;
	/* Event that triggers whenever one of the events in the queue is
	 * ready to activate */
	struct event timeout_event;
	/* The event_base that this timeout list is part of */
	struct event_base *base;
};

/** Mask used to get the real tv_usec value from a common timeout. */
#define COMMON_TIMEOUT_MICROSECONDS_MASK       0x000fffff

struct event_change;

/* List of 'changes' since the last call to eventop.dispatch.  Only maintained
 * if the backend is using changesets. */
struct event_changelist {
	struct event_change *changes;
	int n_changes;
	int changes_size;
};

#ifndef EVENT__DISABLE_DEBUG_MODE
/* Global internal flag: set to one if debug mode is on. */
extern int event_debug_mode_on_;
#define EVENT_DEBUG_MODE_IS_ON() (event_debug_mode_on_)
#else
#define EVENT_DEBUG_MODE_IS_ON() (0)
#endif

TAILQ_HEAD(evcallback_list, event_callback);

/* Sets up an event for processing once */
struct event_once {
	LIST_ENTRY(event_once) next_once;
	struct event ev;

	void (*cb)(evutil_socket_t, short, void *);
	void *arg;
};

/** Contextual information passed from event_base_loop to the "prepare" watcher
 * callbacks. We define this as a struct rather than individual parameters to
 * the callback function for the sake of future extensibility. */
struct evwatch_prepare_cb_info {
	/** The timeout duration passed to the underlying implementation's `dispatch`.
	 * See evwatch_prepare_get_timeout. */
	const struct timeval *timeout;
};

/** Contextual information passed from event_base_loop to the "check" watcher
 * callbacks. We define this as a struct rather than individual parameters to
 * the callback function for the sake of future extensibility. */
struct evwatch_check_cb_info {
	/** Placeholder, since empty struct is not allowed by some compilers. */
	void *unused;
};

/** Watcher types (prepare and check, perhaps others in the future). */
#define EVWATCH_PREPARE 0
#define EVWATCH_CHECK   1
#define EVWATCH_MAX     2

/** Handle to a "prepare" or "check" callback, registered in event_base. */
union evwatch_cb {
	evwatch_prepare_cb prepare;
	evwatch_check_cb check;
};
struct evwatch {
	/** Tail queue pointers, called "next" by convention in libevent.
	 * See <sys/queue.h> */
	TAILQ_ENTRY(evwatch) next;

	/** Pointer to owning event loop */
	struct event_base *base;

	/** Watcher type (see above) */
	unsigned type;

	/** Callback function */
	union evwatch_cb callback;

	/** User-defined argument for callback function */
	void *arg;
};
TAILQ_HEAD(evwatch_list, evwatch);

//是Libevent的结构体
struct event_base {
	/** Function pointers and other data to describe this event_base's
	 * backend.
	 * 初始化Reactor的时候选择一种后端I/0复用机制,并记录在如下字段中. */
	const struct eventop *evsel;
	/** Pointer to backend-specific data.
	 * 指向I/O复用机制真正存储的数据,它通过evsel成员的init函数来初始化. */
	void *evbase;

	/** List of changes to tell backend about at next dispatch.  Only used
	 * by the O(1) backends.
	 * 事件变化队列。其用途是:如果一个文件描述符上注册的事件被多次修改,则可以使用缓冲来
	 * 避免重复的系统调用(比如epoll_ctl)。它仅能用于时间复杂度为0(1)的I/O复用技术*/
	struct event_changelist changelist;

	/** Function pointers used to describe the backend that this event_base
	 * uses for signals
	 * 指向信号的后端处理机制,目前仅在singal.h文件中定义了一种处理方法 */
	const struct eventop *evsigsel;
	/** Data to implement the common signal handler code.
	 * 信号事件处理器使用的数据结构,其中封装了一个由soctetpair创建的管道。它用于信号处
	 * 理函数和事件多路分发器之间的通信*/
	struct evsig_info sig;

	/** Number of virtual events */
	int virtual_event_count;
	/** Maximum number of virtual events active */
	int virtual_event_count_max;
	/** Number of total events added to this event_base */
	int event_count;
	/** Maximum number of total events added to this event_base */
	int event_count_max;
	/** Number of total events active in this event_base */
	int event_count_active;
	/** Maximum number of total events active in this event_base */
	int event_count_active_max;

	/** Set if we should terminate the loop once we're done processing
	 * events.
	 * 是否处理完活动事件队列上剩余的任务之后就退出事件循环.  */
	int event_gotterm;
	/** Set if we should terminate the loop immediately */
	int event_break;
	/** Set if we should start a new instance of the loop immediately.
	 * 是否启动一个新的事件循环 */
	int event_continue;

	/** The currently running priority of events
	 * 目前正在处理的活动事件队列的优先级.  */
	int event_running_priority;

	/** Set if we're running the event_base_loop function, to prevent
	 * reentrant invocation.
	 * 事件循环是否已经启动 */
	int running_loop;

	/** Set to the number of deferred_cbs we've made 'active' in the
	 * loop.  This is a hack to prevent starvation; it would be smarter
	 * to just use event_config_set_max_dispatch_interval's max_callbacks
	 * feature */
	int n_deferreds_queued;

	/* Active event management. */
	/** An array of nactivequeues queues for active event_callbacks (ones
	 * that have triggered, and whose callbacks need to be called).  Low
	 * priority numbers are more important, and stall higher ones.
	 */
	struct evcallback_list *activequeues;
	/** The length of the activequeues array */
	int nactivequeues;
	/** A list of event_callbacks that should become active the next time
	 * we process events, but not this time. */
	struct evcallback_list active_later_queue;

	/* common timeout logic */

	/** An array of common_timeout_list* for all of the common timeout
	 * values we know.
	 * 事件队列，存放IO事件和信号事件处理器. */
	struct common_timeout_list **common_timeout_queues;
	/** The number of entries used in common_timeout_queues */
	int n_common_timeouts;
	/** The total size of common_timeout_queues. */
	int n_common_timeouts_allocated;

	/** Mapping from file descriptors to enabled (added) events */
	struct event_io_map io;

	/** Mapping from signal numbers to enabled (added) events.
	 * 文件描述符和I/O事件之间的映射关系表.     */
	struct event_signal_map sigmap;

	/** Priority queue of events with timeouts.
	 * 时间堆. */
	struct min_heap timeheap;

	/** Stored timeval: used to avoid calling gettimeofday/clock_gettime
	 * too often. */
	struct timeval tv_cache;

	struct evutil_monotonic_timer monotonic_timer;

	/** Difference between internal time (maybe from clock_gettime) and
	 * gettimeofday. */
	struct timeval tv_clock_diff;
	/** Second in which we last updated tv_clock_diff, in monotonic time. */
	time_t last_updated_clock_diff;
	//多线程支持
#ifndef EVENT__DISABLE_THREAD_SUPPORT
	/* threading support */
	/** The thread currently running the event_loop for this base */
	unsigned long th_owner_id;
	/** A lock to prevent conflicting accesses to this event_base
	 * 对event_base的独占锁. */
	void *th_base_lock;
	/** A condition that gets signalled when we're done processing an
	 * event with waiters on it.
	 * 条件变量,用于唤醒正在等待某个事件处理完毕的线程. */
	void *current_event_cond;
	/** Number of threads blocking on current_event_cond. */
	int current_event_waiters;
#endif
	/** The event whose callback is executing right now */
	struct event_callback *current_event;

#ifdef _WIN32
	/** IOCP support structure, if IOCP is enabled. */
	struct event_iocp_port *iocp;
#endif

	/** Flags that this base was configured with
	 * 该event_base的一些配置参数. */
	enum event_base_config_flag flags;

	struct timeval max_dispatch_time;
	int max_dispatch_callbacks;
	int limit_callbacks_after_prio;

	/* Notify main thread to wake up break, etc. */
	/** True if the base already has a pending notify, and we don't need
	 * to add any more.
	 * 下面这组成员变量给工作线程唤醒主线程提供了方法(使用socketPair创建的管道). */
	int is_notify_pending;
	/** A socketpair used by some th_notify functions to wake up the main
	 * thread. */
	evutil_socket_t th_notify_fd[2];
	/** An event used by some th_notify functions to wake up the main
	 * thread. */
	struct event th_notify;
	/** A function used to wake up the main thread from another thread. */
	int (*th_notify_fn)(struct event_base *base);

	/** Saved seed for weak random number generator. Some backends use
	 * this to produce fairness among sockets. Protected by th_base_lock. */
	struct evutil_weakrand_state weakrand_seed;

	/** List of event_onces that have not yet fired. */
	LIST_HEAD(once_event_list, event_once) once_events;

	/** "Prepare" and "check" watchers. */
	struct evwatch_list watchers[EVWATCH_MAX];
};

struct event_config_entry {
	TAILQ_ENTRY(event_config_entry) next;

	const char *avoid_method;
};

/** Internal structure: describes the configuration we want for an event_base
 * that we're about to allocate. */
struct event_config {
	TAILQ_HEAD(event_configq, event_config_entry) entries;

	int n_cpus_hint;
	struct timeval max_dispatch_interval;
	int max_dispatch_callbacks;
	int limit_callbacks_after_prio;
	enum event_method_feature require_features;
	enum event_base_config_flag flags;
};

/* Internal use only: Functions that might be missing from <sys/queue.h> */
#ifndef LIST_END
#define LIST_END(head)			NULL
#endif

#ifndef TAILQ_FIRST
#define	TAILQ_FIRST(head)		((head)->tqh_first)
#endif
#ifndef TAILQ_END
#define	TAILQ_END(head)			NULL
#endif
#ifndef TAILQ_NEXT
#define	TAILQ_NEXT(elm, field)		((elm)->field.tqe_next)
#endif

#ifndef TAILQ_FOREACH
#define TAILQ_FOREACH(var, head, field)					\
	for ((var) = TAILQ_FIRST(head);					\
	     (var) != TAILQ_END(head);					\
	     (var) = TAILQ_NEXT(var, field))
#endif

#ifndef TAILQ_INSERT_BEFORE
#define	TAILQ_INSERT_BEFORE(listelm, elm, field) do {			\
	(elm)->field.tqe_prev = (listelm)->field.tqe_prev;		\
	(elm)->field.tqe_next = (listelm);				\
	*(listelm)->field.tqe_prev = (elm);				\
	(listelm)->field.tqe_prev = &(elm)->field.tqe_next;		\
} while (0)
#endif

#define N_ACTIVE_CALLBACKS(base)					\
	((base)->event_count_active)

int evsig_set_handler_(struct event_base *base, int evsignal,
			  void (*fn)(int));
int evsig_restore_handler_(struct event_base *base, int evsignal);

int event_add_nolock_(struct event *ev,
    const struct timeval *tv, int tv_is_absolute);
/** Argument for event_del_nolock_. Tells event_del not to block on the event
 * if it's running in another thread. */
#define EVENT_DEL_NOBLOCK 0
/** Argument for event_del_nolock_. Tells event_del to block on the event
 * if it's running in another thread, regardless of its value for EV_FINALIZE
 */
#define EVENT_DEL_BLOCK 1
/** Argument for event_del_nolock_. Tells event_del to block on the event
 * if it is running in another thread and it doesn't have EV_FINALIZE set.
 */
#define EVENT_DEL_AUTOBLOCK 2
/** Argument for event_del_nolock_. Tells event_del to proceed even if the
 * event is set up for finalization rather for regular use.*/
#define EVENT_DEL_EVEN_IF_FINALIZING 3
int event_del_nolock_(struct event *ev, int blocking);
int event_remove_timer_nolock_(struct event *ev);

void event_active_nolock_(struct event *ev, int res, short count);
EVENT2_EXPORT_SYMBOL
int event_callback_activate_(struct event_base *, struct event_callback *);
int event_callback_activate_nolock_(struct event_base *, struct event_callback *);
int event_callback_cancel_(struct event_base *base,
    struct event_callback *evcb);

void event_callback_finalize_nolock_(struct event_base *base, unsigned flags, struct event_callback *evcb, void (*cb)(struct event_callback *, void *));
EVENT2_EXPORT_SYMBOL
void event_callback_finalize_(struct event_base *base, unsigned flags, struct event_callback *evcb, void (*cb)(struct event_callback *, void *));
int event_callback_finalize_many_(struct event_base *base, int n_cbs, struct event_callback **evcb, void (*cb)(struct event_callback *, void *));


EVENT2_EXPORT_SYMBOL
void event_active_later_(struct event *ev, int res);
void event_active_later_nolock_(struct event *ev, int res);
int event_callback_activate_later_nolock_(struct event_base *base,
    struct event_callback *evcb);
int event_callback_cancel_nolock_(struct event_base *base,
    struct event_callback *evcb, int even_if_finalizing);
void event_callback_init_(struct event_base *base,
    struct event_callback *cb);

/* FIXME document. */
EVENT2_EXPORT_SYMBOL
void event_base_add_virtual_(struct event_base *base);
void event_base_del_virtual_(struct event_base *base);

/** For debugging: unless assertions are disabled, verify the referential
    integrity of the internal data structures of 'base'.  This operation can
    be expensive.

    Returns on success; aborts on failure.
*/
EVENT2_EXPORT_SYMBOL
void event_base_assert_ok_(struct event_base *base);
void event_base_assert_ok_nolock_(struct event_base *base);


/* Helper function: Call 'fn' exactly once every inserted or active event in
 * the event_base 'base'.
 *
 * If fn returns 0, continue on to the next event. Otherwise, return the same
 * value that fn returned.
 *
 * Requires that 'base' be locked.
 */
int event_base_foreach_event_nolock_(struct event_base *base,
    event_base_foreach_event_cb cb, void *arg);

/* Cleanup function to reset debug mode during shutdown.
 *
 * Calling this function doesn't mean it'll be possible to re-enable
 * debug mode if any events were added.
 */
void event_disable_debug_mode(void);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_INTERNAL_H_INCLUDED_ */
