require 'mkmf'

$CFLAGS = "-O3"

have_func('rb_tracepoint_enable')

gc_event = have_const('RUBY_INTERNAL_EVENT_NEWOBJ')

if gc_event
  create_makefile('alloc_track/alloc_track')
else
  File.open('Makefile', 'w') do |f|
    f.puts "install:\n\t\n"
  end
end
