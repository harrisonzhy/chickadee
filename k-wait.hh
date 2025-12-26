#ifndef CHICKADEE_K_WAIT_HH
#define CHICKADEE_K_WAIT_HH
#include "kernel.hh"
#include "k-waitstruct.hh"

// k-wait.hh
//    Defines `waiter` and `wait_queue` member functions.
//    `k-waitstruct.hh` defines the `waiter` and `wait_queue` types.
//    (Separating the structures and functions into different header files
//    avoids problems with circular dependencies.)

extern wait_queue waitpid_wq;

inline waiter::waiter() {
}

inline waiter::~waiter() {
    assert(!links_.is_linked());
    // Feel free to add more sanity checks if you’d like!
}

inline void waiter::prepare(wait_queue& wq) {
    assert(!links_.is_linked());
    p_ = current();
    wq_ = &wq;
    auto irqs = wq_->lock_.lock();
    p_->pstate_ = proc::ps_blocked;
    wq_->q_.push_back(this);
    wq_->lock_.unlock(irqs);
}

inline void waiter::maybe_block() {
    assert(p_ == current() && wq_ != nullptr);
    // Thanks to concurrent wakeups, `p_->pstate_` might or might not equal
    // `proc::ps_blocked`, and `links_` might or might not be linked.
    // When the function returns, `p_->pstate_` MUST NOT equal
    // `proc::ps_blocked`, and `links_` MUST NOT be linked.
    if (p_->pstate_ == proc::ps_blocked) {
        p_->yield();
    }
    auto irqs = wq_->lock_.lock();
    if (links_.is_linked()) {
        wq_->q_.erase(this);
    }
    assert(p_->pstate_ != proc::ps_blocked && !links_.is_linked());
    wq_->lock_.unlock(irqs);
}

inline void waiter::clear() {
    assert(p_ == current() && wq_ != nullptr);
    auto irqs = wq_->lock_.lock();
    if (links_.is_linked()) {
        wq_->q_.erase(this);
    }
    if (p_->pstate_ == proc::ps_blocked) {
        p_->pstate_ = proc::ps_runnable;
    }
    wq_->lock_.unlock(irqs);
}

inline void waiter::wake() {
    assert(!links_.is_linked());
    p_->unblock();
}

// waiter::block_until(wq, predicate)
//    Block on `wq` until `predicate()` returns true.
template <typename F>
inline void waiter::block_until(wait_queue& wq, F predicate) {
    while (true) {
        prepare(wq);
        if (p_->exiting_ && p_->pstate_ != proc::ps_exited) {
            waitpid_wq.wake_all();
            cli();
            p_->yield_noreturn();
        } else if (predicate()) {
            break;
        }
        maybe_block();
    }
    clear();
}

// waiter::block_until(wq, predicate, lock, irqs)
//    Block on `wq` until `predicate()` returns true. The `lock`
//    must be locked; it is unlocked before blocking (if blocking
//    is necessary). All calls to `predicate` have `lock` locked,
//    and `lock` is locked on return.
template <typename F>
inline void waiter::block_until(wait_queue& wq, F predicate,
                                spinlock& lock, irqstate& irqs) {
    while (true) {
        prepare(wq);
        if (p_->exiting_ && p_->pstate_ != proc::ps_exited) {
            lock.unlock(irqs);
            waitpid_wq.wake_all();
            cli();
            p_->yield_noreturn();
        } else if (predicate()) {
            break;
        }
        lock.unlock(irqs);
        maybe_block();
        irqs = lock.lock();
    }
    clear();
}

// waiter::block_until(wq, predicate, guard)
//    Block on `wq` until `predicate()` returns true. The `guard`
//    must be locked on entry; it is unlocked before blocking (if
//    blocking is necessary) and locked on return.
template <typename F>
inline void waiter::block_until(wait_queue& wq, F predicate,
                                spinlock_guard& guard) {
    block_until(wq, predicate, guard.lock_, guard.irqs_);
}

// wait_queue::wake_all()
//    Lock the wait queue, then clear it by waking all waiters.
inline void wait_queue::wake_all() {
    spinlock_guard guard(lock_);
    while (auto w = q_.pop_front()) {
        w->wake();
    }
}

struct timing_wheel {
    static constexpr size_t num_wqs_ = 16;
    wait_queue wqs_[num_wqs_];
};

#endif
