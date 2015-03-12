#include "ruby/ruby.h"
#include "ruby/intern.h"
#include "ruby/debug.h"

#define ALLOCTRACK_OBJ_BIT FL_USER19

#define MAX(_x, _y) ( (_x) > (_y) ? (_x) : (_y) )

typedef struct stat_collector {
  struct stat_collector *next; /* not currently used */
  VALUE thread;
  int64_t current_alloc;
  int64_t current_free;
  int64_t current_limit;
  int     limit_signal;
  int64_t max_delta;
} stat_collector_t;

static VALUE mAllocTrack;
static VALUE tpval, tpval_exception;
static VALUE eAllocTrackError, eAllocTrackLimitExceeded;
static stat_collector_t *root_collector, *current_collector;

#define LOG(s) fprintf(stderr, s); fflush(stderr);

static stat_collector_t *
add_collector(VALUE thread)
{
  stat_collector_t *c = (stat_collector_t *) calloc(1, sizeof(*c));
  c->thread = thread;
  if (root_collector) {
    c->next = root_collector;
    root_collector = c;
  } else {
    root_collector = c;
    rb_tracepoint_enable(tpval);
  }
  return c;
}

static void
remove_collector(VALUE thread)
{
  stat_collector_t *c, *prev = NULL;

  for (c = root_collector; c != NULL; prev = c, c = c->next) {
    if (c->thread == thread) {
      if (!prev) {
        root_collector = c->next;
      } else {
        prev->next = c->next;
      }
      current_collector = NULL;
      free(c);
      break;
    }
  }

  if (!root_collector) {
    rb_tracepoint_disable(tpval);
  }
}

static stat_collector_t *
get_collector(VALUE thread)
{
  stat_collector_t *c;

  if (current_collector && current_collector->thread == thread) {
    return current_collector;
  }

  for (c = root_collector; c != NULL; c = c->next) {
    if (c->thread == thread) {
      /* cache the collector so we don't have to scan the list every time */
      current_collector = c;
      return c;
    }
  }

  return NULL;
}

static VALUE
started()
{
  return get_collector(rb_thread_current()) ? Qtrue : Qfalse;
}

static void
validate_started()
{
  if (!started()) {
    rb_raise(eAllocTrackError, "allocation tracker has not been started");
  }
}

static void
validate_stopped()
{
  if (started()) {
    rb_raise(eAllocTrackError, "allocation tracker already started");
  }
}

static VALUE
start()
{
  validate_stopped();
  /* TODO: support multiple running trackers */
  if (root_collector) {
    rb_raise(eAllocTrackError, "allocation tracker already running on another thread");
  }
  add_collector(rb_thread_current());
  return Qnil;
}

static VALUE
stop()
{
  validate_started();
  remove_collector(rb_thread_current());
  return Qnil;
}

static VALUE
alloc()
{
  validate_started();
  return LL2NUM(get_collector(rb_thread_current())->current_alloc);
}

static VALUE
_free()
{
  validate_started();
  return LL2NUM(get_collector(rb_thread_current())->current_free);
}

static VALUE
delta()
{
  stat_collector_t *c;
  validate_started();
  c = get_collector(rb_thread_current());
  return LL2NUM(c->current_alloc - c->current_free);
}

static VALUE
max_delta()
{
  stat_collector_t *c;
  validate_started();
  c = get_collector(rb_thread_current());
  return LL2NUM(c->max_delta);
}

static VALUE
do_limit(VALUE arg)
{
  start();
  get_collector(rb_thread_current())->current_limit = FIX2INT(arg);
  return rb_yield(Qnil);
}

static VALUE
ensure_stopped(VALUE arg)
{
  if (started()) {
    stop();
  }
  return Qnil;
}

static VALUE
limit(VALUE self, VALUE num_allocs)
{
  if (!rb_block_given_p()) {
    rb_raise(rb_eArgError, "block required");
  }
  if (!RB_TYPE_P(num_allocs, T_FIXNUM)) {
    rb_raise(rb_eArgError, "limit() must be passed a number");
  }
  validate_stopped();
  return rb_ensure(do_limit, num_allocs, ensure_stopped, Qnil);
}

static int
is_collector_limit_exceeded(stat_collector_t *c)
{
  return c->limit_signal;
}

static void
tracepoint_hook(VALUE tpval, void *data)
{
  stat_collector_t *c;
  rb_trace_arg_t *tparg = rb_tracearg_from_tracepoint(tpval);
  rb_event_flag_t flag = rb_tracearg_event_flag(tparg);
  VALUE obj = rb_tracearg_object(tparg);
  switch(flag) {
    case RUBY_INTERNAL_EVENT_NEWOBJ:
      if ((c = get_collector(rb_thread_current())) != NULL) {
        RBASIC(obj)->flags |= ALLOCTRACK_OBJ_BIT;
        c->current_alloc++;
        c->max_delta = MAX( c->current_alloc - c->current_free, c->max_delta);

        if (c->current_limit && (c->current_alloc - c->current_free) > c->current_limit) {
          c->limit_signal = 1;
          if (!rb_tracepoint_enabled_p(tpval_exception)) {
            /*
              it's not safe to raise an exception from an internal event handler.
              in order to get around this, we enable a normal tracepoint on all
              events and raise from there.
            */
            rb_tracepoint_enable(tpval_exception);
          }
        }
      }
      break;
    case RUBY_INTERNAL_EVENT_FREEOBJ:
      if ((c = get_collector(rb_thread_current())) != NULL && (RBASIC(obj)->flags & ALLOCTRACK_OBJ_BIT)) {
        c->current_free++;
      }
      break;
  }
}

static int
any_collectors_with_exceeded_limits()
{
  stat_collector_t *c;
  for (c = root_collector; c != NULL; c = c->next) {
    if (c->limit_signal) {
      return 1;
    }
  }
  return 0;
}

static void
exception_tracepoint_hook(VALUE tpval, void *data)
{
  int should_raise = 0;
  VALUE th = rb_thread_current();
  stat_collector_t *c;
  if ((c = get_collector(th)) != NULL && is_collector_limit_exceeded(c)) {
    rb_gc(); /* try to gc before raising */
    if ((c->current_alloc - c->current_free) > c->current_limit) {
      remove_collector(th);
      should_raise = 1;
    } else {
      c->limit_signal = 0;
    }
    if (!any_collectors_with_exceeded_limits()) {
      rb_tracepoint_disable(tpval_exception);
    }

    if (should_raise) rb_raise(eAllocTrackLimitExceeded, "allocation limit exceeded");
  }
}

void
Init_alloc_track()
{
  mAllocTrack = rb_define_module("AllocTrack");

  rb_define_singleton_method(mAllocTrack, "start", start, 0);
  rb_define_singleton_method(mAllocTrack, "started?", started, 0);
  rb_define_singleton_method(mAllocTrack, "stop", stop, 0);
  rb_define_singleton_method(mAllocTrack, "alloc", alloc, 0);
  rb_define_singleton_method(mAllocTrack, "free", _free, 0);
  rb_define_singleton_method(mAllocTrack, "delta", delta, 0);
  rb_define_singleton_method(mAllocTrack, "limit", limit, 1);
  rb_define_singleton_method(mAllocTrack, "max_delta", max_delta, 0);

  eAllocTrackError = rb_define_class_under(mAllocTrack, "Error", rb_eStandardError);
  eAllocTrackLimitExceeded = rb_define_class_under(mAllocTrack, "LimitExceeded", rb_eStandardError);

  tpval = rb_tracepoint_new(0, RUBY_INTERNAL_EVENT_NEWOBJ|RUBY_INTERNAL_EVENT_FREEOBJ, tracepoint_hook, NULL);
  tpval_exception = rb_tracepoint_new(0, RUBY_EVENT_TRACEPOINT_ALL, exception_tracepoint_hook, NULL);

  rb_gc_register_mark_object(tpval);
  rb_gc_register_mark_object(tpval_exception);
}
