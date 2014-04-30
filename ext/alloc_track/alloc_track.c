#include "ruby/ruby.h"
#include "ruby/intern.h"
#include "ruby/debug.h"

static VALUE mAllocTrack;
static VALUE tpval, tpval_exception;
static int current_alloc, current_free, current_limit;
static VALUE start_thread;
static VALUE eAllocTrackLimitExceeded;

#define LOG(s) fprintf(stderr, s); fflush(stderr);

static void
validate_thread()
{
  if (rb_thread_current() != start_thread) rb_raise(rb_eRuntimeError, "called on invalid thread");
}

static VALUE
started()
{
  return rb_tracepoint_enabled_p(tpval);
}

static void
validate_started()
{
  if (!started()) {
    rb_raise(rb_eRuntimeError, "allocation tracker has not been started");
  }
}

static void
validate_stopped()
{
  if (started()) {
    rb_raise(rb_eRuntimeError, "allocation tracker already started");
  }
}

static VALUE
start()
{
  validate_stopped();
  start_thread = rb_thread_current();
  current_alloc = current_free = current_limit = 0;
  rb_tracepoint_enable(tpval);

  return Qnil;
}

static VALUE
stop()
{
  validate_started();
  validate_thread();
  rb_tracepoint_disable(tpval);
  return Qnil;
}

static VALUE
alloc()
{
  validate_started();
  validate_thread();
  return INT2FIX(current_alloc);
}

static VALUE
_free()
{
  validate_started();
  validate_thread();
  return INT2FIX(current_free);
}

static VALUE
delta()
{
  validate_started();
  validate_thread();
  return INT2FIX(current_alloc - current_free);
}

static VALUE
do_limit(VALUE arg)
{
  start();
  current_limit = FIX2INT(arg);
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
is_start_thread()
{
  return rb_thread_current() == start_thread ? 1 : 0;
}

static void
tracepoint_hook(VALUE tpval, void *data)
{
  rb_trace_arg_t *tparg = rb_tracearg_from_tracepoint(tpval);
  rb_event_flag_t flag = rb_tracearg_event_flag(tparg);
  switch(flag) {
    case RUBY_INTERNAL_EVENT_NEWOBJ:
      if (is_start_thread()) {
        ++current_alloc;
        if (current_limit != 0 && (current_alloc  - current_free) > current_limit) {
          stop();
          /*
            it's not safe to raise an exception from an internal event handler.
            in order to get around this, we enable a normal tracepoint on all
            events and raise from there.
          */
          rb_tracepoint_enable(tpval_exception);
        }
      }
      break;
    case RUBY_INTERNAL_EVENT_FREEOBJ:
      if (is_start_thread()) ++current_free;
      break;
  }
}

static void
exception_tracepoint_hook(VALUE tpval, void *data)
{
  if (is_start_thread()) {
    rb_tracepoint_disable(tpval_exception);
    rb_raise(eAllocTrackLimitExceeded, "allocation limit exceeded");
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

  eAllocTrackLimitExceeded = rb_define_class_under(mAllocTrack, "LimitExceeded", rb_eStandardError);

  tpval = rb_tracepoint_new(0, RUBY_INTERNAL_EVENT_NEWOBJ|RUBY_INTERNAL_EVENT_FREEOBJ, tracepoint_hook, NULL);
  tpval_exception = rb_tracepoint_new(0, RUBY_EVENT_TRACEPOINT_ALL, exception_tracepoint_hook, NULL);

  rb_gc_register_mark_object(tpval);
  rb_gc_register_mark_object(tpval_exception);
}
