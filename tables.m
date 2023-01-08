#!/usr/bin/octave -qf

pkg load signal;

%---------------------------------------------------------------
HEADER = "\
/*\n\
 */\n\
#define VOLSTEPS %d\n\
\n\
const float scale[VOLSTEPS] = {\n\
%s\
};\n\
\n\
const int16_t db[VOLSTEPS] = {\n\
%s\
};\n\
\n\
";

FIR = "\
#define UPSAMPLE_SHIFT_%d\t\t%d\n\
#define NUMTAPS%d\t\t\t%d\n\
\n\
const float hc%d[] = {\n\
%s\
};\n\
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

function s = carray(v)
  s = sprintf(["\t%.8ff,\n"], v);
endfunction

function s = sfir(fmt, e, n)

  f = 2^e;
  s = sprintf(fmt,
              f, e,
              f, n,
              f, carray(retap(f, f * fir1(n-1, 1/f))));

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

%---------------------------------------------------------------
VOLSTEPS=64;
ATTN = 10^(-3/20);

function x = db(x)
  x = 20 * log10(x);
endfunction

function x = vol(x)
  x = exp(log(1000) * x) / 1000;
endfunction

VOL = vol([1:-1/(VOLSTEPS-1):0]);

%---------------------------------------------------------------
av = argv();
fd = fopen(av{1}, "w");

fprintf(fd, HEADER, VOLSTEPS, carray(VOL),
        sprintf(["\t%12d,\n"], fix(256 * db(VOL))));
fprintf(fd, "%s\n", sfir(FIR, 4, 32));
fprintf(fd, "%s\n", sfir(FIR, 3, 16));
fprintf(fd, "%s\n", sample(SAMPLE, 1, 48000));
