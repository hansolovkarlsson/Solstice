program TestUntypedfileBadelemString;

{ An array whose element type isn't typed-file-safe (string) - rejected
  at compile time with a clear message. }

var
    f: file;
    buf: array[0..2] of string;

begin
    assign(f, '/tmp/untypedfile_badelem_string.bin');
    rewrite(f);
    BlockWrite(f, buf, 1);
    close(f);
end.
