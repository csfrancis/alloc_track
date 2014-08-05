Gem::Specification.new do |s|
  s.name = 'alloc_track'
  s.version = '0.0.2'
  s.summary = 'allocation tracker for ruby 2.1+'
  s.description = 'tracks memory allocations with rgengc in ruby 2.1'

  s.homepage = 'https://github.com/csfrancis/alloc_track'
  s.authors = 'Scott Francis'
  s.email   = 'scott.francis@shopify.com'
  s.license = 'MIT'

  s.files = `git ls-files`.split("\n")
  s.extensions = ['ext/alloc_track/extconf.rb']
  s.add_development_dependency 'rake-compiler', '~> 0.9'
end
