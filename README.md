ffmpeg -y -i "demo1.mp4" \
  -map 0:v:0 -map 0:a:0 -sn -dn -map_metadata -1 \
  -sws_flags lanczos+accurate_rnd+full_chroma_inp \
  -c:v mjpeg -q:v 5 -r 20 -pix_fmt yuvj420p \
  -vf "hqdn3d=1.2:1.2:2.5:2.5,scale=480:272:flags=lanczos:force_original_aspect_ratio=decrease,\
pad=480:272:(ow-iw)/2:(oh-ih)/2:black,fps=12,unsharp=5:5:0.6:5:5:0.0,gradfun=10" \
  -c:a mp3 -b:a 96k -ac 1 -ar 44100 \
  "demo1.avi"