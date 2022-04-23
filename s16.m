#!/usr/bin/octave -qf

av = argv();
fn=av{1};
fs=str2num(av{2});

fd = fopen([fn, ".s16"], "w");

switch (fn)
  case "s1"
    n=192;
    s = sin(2*pi*[0:n-1]*1000/fs);
  case "s2"
    n=96;
    s0 = sin(2*pi*[0:n-1]*1000/fs);
    s1 = fliplr(sin(2*pi*[1:n]*1000/fs));
    s = reshape ([s0; s1], 1, []);
  case "s3"
    n=960;
    s0 = sin(2*pi*[0:n-1]*50/fs);
    s1 = sin(2*pi*[0:n-1]*1000/fs);
    s2 = sin(2*pi*(0:n-1)*20000/fs);
    s = 0.3 * s0 + 0.5 * s1 + 0.2 * s2;
endswitch

fwrite(fd, fix(s * 2^15), "int16");
