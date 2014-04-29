require 'mkmf'
have_func('rb_tracepoint_enable')
have_func('rb_threadptr_pending_interrupt_enque')
have_func('rb_threadptr_interrupt')

gc_event = have_const('RUBY_INTERNAL_EVENT_NEWOBJ')

if gc_event
  create_makefile('alloc_tracker/alloc_track')
else
  File.open('Makefile', 'w') do |f|
    f.puts "install:\n\t\n"
  end
end
