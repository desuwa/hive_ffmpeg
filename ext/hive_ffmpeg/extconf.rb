require 'mkmf'

dir_config('ffmpeg')

$CFLAGS << ' -funroll-loops -Wall'

unless have_header('libavformat/avformat.h') && have_library('avformat') &&
  have_header('libavcodec/avcodec.h') && have_library('avcodec') &&
  have_header('libswscale/swscale.h') && have_library('swscale')
  abort 'Missing FFmpeg'
end

if with_config('jpeg', true)
  dir_config('jpeg')

  unless have_header('jpeglib.h') && have_library('jpeg')
    abort 'Missing libjpeg'
  end
end

if with_config('png', false)
  dir_config('png')

  unless have_header('png.h') && have_library('png')
    abort 'Missing libpng'
  end
end

create_makefile('hive_ffmpeg')
