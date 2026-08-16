program TestUntypedfileBadeof;

{ BlockRead past actual end of file - confirms the existing fatal-
  runtime-error convention (not a crash, not silently returning
  garbage/zero), matching typed files' own "attempted to read past the
  end" error. }

var
    f: file;
    buf: array[0..2] of integer;

begin
    buf[0] := 1;
    assign(f, '/tmp/untypedfile_badeof.bin');
    rewrite(f);
    BlockWrite(f, buf, 1);
    close(f);

    reset(f);
    BlockRead(f, buf, 3);
    close(f);
end.
