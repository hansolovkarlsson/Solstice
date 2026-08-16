program TestUntypedfileNotopen;

{ BlockRead before reset/rewrite - confirms whatever existing "file not
  open" check the underlying opcodes already perform fires correctly
  here too. }

var
    f: file;
    buf: array[0..2] of integer;

begin
    assign(f, '/tmp/untypedfile_notopen.bin');
    BlockRead(f, buf, 1);
end.
