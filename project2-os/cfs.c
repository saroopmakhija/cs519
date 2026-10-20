// ============ DATA STRUCTURES ============

#define NICE_0_LOAD       1024
#define TARGET_LATENCY    20    // ms
#define MIN_GRANULARITY   1     // ms
#define BALANCE_THRESHOLD 256   // load units
#define BALANCE_PERIOD_MS 4

struct task {
    int      id;
    uint64_t weight;       // W_i, from prio_to_weight[]
    double   util;         // C_i, EWMA tracked
    uint64_t vruntime;
    uint64_t exec_start;   // when placed on CPU
    int      state;        // RUNNING | READY | BLOCKED | DONE
    int      cpu;          // current rq
};

struct rq {
    int             cpu;
    rbtree<task*>   tree;          // keyed by vruntime; cached leftmost
    task           *curr;
    uint64_t        min_vruntime;
    uint64_t        total_weight;  // Σ W_i over runnable
    int             nr_running;
    spinlock_t      lock;
};

struct rq runqueues[N];   // one per core


// ============ CORE OPERATIONS ============

// fold task into rq, set initial vruntime
void enqueue(struct rq *rq, struct task *t, bool is_new) {
    lock(rq);
    if (is_new)
        t->vruntime = rq->min_vruntime;
    else  // wakeup case
        t->vruntime = max(t->vruntime, rq->min_vruntime - TARGET_LATENCY/2);
    
    rb_insert(&rq->tree, t, t->vruntime);
    rq->total_weight += t->weight;
    rq->nr_running++;
    t->state = READY;
    t->cpu = rq->cpu;
    unlock(rq);
}

void dequeue(struct rq *rq, struct task *t) {
    lock(rq);
    rb_remove(&rq->tree, t);
    rq->total_weight -= t->weight;
    rq->nr_running--;
    update_min_vruntime(rq);
    unlock(rq);
}

// advance curr's vruntime by elapsed wall-clock time
void update_curr(struct rq *rq) {
    if (!rq->curr) return;
    uint64_t now = get_time();
    uint64_t delta = now - rq->curr->exec_start;
    rq->curr->vruntime += delta * NICE_0_LOAD / rq->curr->weight;
    rq->curr->exec_start = now;
    update_min_vruntime(rq);
}

void update_min_vruntime(struct rq *rq) {
    uint64_t leftmost_vr = rq->tree.empty()
        ? rq->min_vruntime
        : rb_leftmost(&rq->tree)->vruntime;
    rq->min_vruntime = max(rq->min_vruntime, leftmost_vr);
}

// compute this task's slice given current load
uint64_t timeslice_for(struct rq *rq, struct task *t) {
    uint64_t slice = TARGET_LATENCY * t->weight / rq->total_weight;
    return max(slice, (uint64_t)MIN_GRANULARITY);
}


// ============ SCHEDULER MAIN PATH ============

// called from timer tick, every 1ms, on each CPU
void scheduler_tick(int cpu) {
    struct rq *rq = &runqueues[cpu];
    lock(rq);
    update_curr(rq);
    
    if (rq->curr) {
        uint64_t ran = get_time() - rq->curr->exec_start_slice;
        if (ran >= timeslice_for(rq, rq->curr))
            set_resched(rq->curr);
        else if (!rq->tree.empty() &&
                 rb_leftmost(&rq->tree)->vruntime + GRAN < rq->curr->vruntime)
            set_resched(rq->curr);
    }
    unlock(rq);
    
    // periodic load balance
    if (cpu == designated_balancer() && tick_count % BALANCE_PERIOD_TICKS == 0)
        load_balance(cpu);
}

// called when curr is preempted / blocks / exits
struct task *pick_next_task(int cpu) {
    struct rq *rq = &runqueues[cpu];
    lock(rq);
    
    // re-insert prev curr if still runnable
    if (rq->curr && rq->curr->state == READY) {
        update_curr(rq);
        rb_insert(&rq->tree, rq->curr, rq->curr->vruntime);
    }
    
    if (rq->tree.empty()) {
        rq->curr = NULL;
        unlock(rq);
        return NULL;  // CPU goes idle, may trigger balance pull
    }
    
    struct task *next = rb_leftmost(&rq->tree);
    rb_remove(&rq->tree, next);
    next->state = RUNNING;
    next->exec_start = get_time();
    next->exec_start_slice = next->exec_start;
    rq->curr = next;
    unlock(rq);
    return next;
}


// ============ MULTICORE: LOAD BALANCE ============

// load metric incorporates utilization (matches the question's C_i)
double rq_load(struct rq *rq) {
    double sum = 0;
    for (task t : rq->tree) sum += t->weight * t->util;
    if (rq->curr) sum += rq->curr->weight * rq->curr->util;
    return sum;
}

void load_balance(int this_cpu) {
    struct rq *this_rq = &runqueues[this_cpu];
    int busiest = -1;
    double busiest_load = rq_load(this_rq);
    
    for (int c = 0; c < N; c++) {
        if (c == this_cpu) continue;
        double l = rq_load(&runqueues[c]);
        if (l > busiest_load) { busiest_load = l; busiest = c; }
    }
    if (busiest < 0) return;
    
    double imbalance = busiest_load - rq_load(this_rq);
    if (imbalance < BALANCE_THRESHOLD) return;
    
    // pull tasks until ~half the imbalance is moved
    double to_pull = imbalance / 2;
    double pulled = 0;
    struct rq *src = &runqueues[busiest];
    
    lock_two(src, this_rq);
    for (task *t : src->tree) {
        if (pulled >= to_pull) break;
        if (!can_migrate(t)) continue;        // affinity check
        
        // re-base vruntime across runqueues
        t->vruntime -= src->min_vruntime;
        rb_remove(&src->tree, t);
        src->total_weight -= t->weight;
        src->nr_running--;
        
        t->vruntime += this_rq->min_vruntime;
        rb_insert(&this_rq->tree, t, t->vruntime);
        this_rq->total_weight += t->weight;
        this_rq->nr_running++;
        t->cpu = this_cpu;
        pulled += t->weight * t->util;
    }
    unlock_two(src, this_rq);
}


// ============ ENTRY POINT (per CPU) ============

void cpu_main_loop(int cpu) {
    while (1) {
        struct task *next = pick_next_task(cpu);
        if (!next) {
            load_balance(cpu);   // try to pull work
            if (rq_empty(cpu)) idle();
            continue;
        }
        run_task_until_resched(next);  // returns on tick, block, exit
        // loop back: pick_next_task handles re-enqueueing curr
    }
}