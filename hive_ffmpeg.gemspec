Gem::Specification.new do |s|
  s.name = 'hive_ffmpeg'
  s.version = '0.0.2'
  s.summary = 'FFmpeg Ruby bindings'
  s.author = 'Maxime Youdine'
  s.license = 'MIT'
  s.homepage = 'https://github.com/desuwa/hive_ffmpeg'
  s.required_ruby_version = '>= 1.9.3'
  s.files = %w[
    hive_ffmpeg.gemspec
    lib/hive_ffmpeg/hive_ffmpeg.rb
    ext/hive_ffmpeg/extconf.rb
    ext/hive_ffmpeg/hive_ffmpeg.c
  ]
  s.extensions = ['ext/hive_ffmpeg/extconf.rb']
  s.require_paths = ['lib']
end
