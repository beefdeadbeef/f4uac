#!/usr/bin/octave -qf

pkg load signal;

%---------------------------------------------------------------
VOLSTEPS=32;

%---------------------------------------------------------------
HEADER = "\
/*\n\
 */\n\
\n\
#define VOLSTEPS %d\n\
\n\
";

FIR = "\
/*\n\
 */\n\
#define UPSAMPLE_SHIFT_%d\t\t%d\n\
#define NUMTAPS%d\t\t\t%d\n\
\n\
__asm__ (\n\
	\".pushsection .rodata,\\\"a\\\"\\n\"\n\
	\".global hc%d\\n\"\n\
	\" hc%d:\\n\"\n\
	\".incbin \\\"hc%d.bin\\\"\\n\"\n\
	\".popsection\\n\"\n\
	);\n\
\n\
extern const float hc%d[%d];\n\
";

SAMPLE = "\
#define S%dLEN %d\n\
\n\
__asm__ (\n\
	\".pushsection .rodata,\\\"a\\\"\\n\"\n\
	\".global s%d_tbl\\n\"\n\
	\" s%d_tbl:\\n\"\n\
	\".incbin \\\"s%d.bin\\\"\\n\"\n\
	\".popsection\\n\"\n\
	);\n\
\n\
extern const float s%d_tbl[S%dLEN];\n\
\n\
";

%---------------------------------------------------------------
function o = retap(u, v)

  o = [];
  n = length(v);
  ph = n / u;

  for i = [u:-1:1]
    t = i;
    for k = [ph:-1:1]
      o = [o, v(t)]; t = t + u;
    endfor
  endfor

endfunction

function s = sample(fmt, x, fs)

  switch (x)
    case 1
      n=384;
      v0 = sin(2*pi*[0:n-1]*1000/fs);
      v1 = fliplr(sin(2*pi*[1:n]*1000/fs));
      v = reshape ([v0; v1], 1, []);
    case 2
      n=3840;
      v0 = sin(2*pi*[0:n-1]*50/fs);
      v1 = sin(2*pi*[0:n-1]*1000/fs);
      v2 = sin(2*pi*(0:n-1)*20000/fs);
      v = 0.3 * v0 + 0.5 * v1 + 0.2 * v2;
  endswitch

  s = sprintf(fmt,
              x, length(v),
              x, x, x,
              x, x);

  fd = fopen(sprintf("s%d.bin", x), "w");
  fwrite(fd, v, "float");
  fclose(fd);

endfunction

function s = sfir(fmt, e, n, v)

  f = 2^e;
  bin = [];

  s = sprintf(fmt,
              f, e,
              f, n,
              f, f, f, f, n * length(v));

  for m = v
    bin = [bin, m * f * retap(f, fir1(n-1, 1/f))];
  endfor

  fd = fopen(sprintf("hc%d.bin", f), "w");
  fwrite(fd, bin, "float");
  fclose(fd);

endfunction

%---------------------------------------------------------------
av = argv();
fd = fopen(av{1}, "w");

vol = 10 .^ (-[3:VOLSTEPS+2] / 20);

fprintf(fd, HEADER, VOLSTEPS);
fprintf(fd, "%s\n", sfir(FIR, 4, 32, vol));
fprintf(fd, "%s\n", sfir(FIR, 3, 16, vol));
fprintf(fd, "%s\n", sample(SAMPLE, 1, 48000));
fprintf(fd, "%s\n", sample(SAMPLE, 2, 48000));
