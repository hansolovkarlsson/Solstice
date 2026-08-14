program TestUntypedfileBadelemRecord;

{ An array whose element type isn't typed-file-safe (a record) -
  rejected at compile time with a clear message. }

type
    TPoint = record
        x, y: integer;
    end;

var
    f: file;
    buf: array[0..2] of TPoint;

begin
    assign(f, '/tmp/untypedfile_badelem_record.bin');
    rewrite(f);
    BlockWrite(f, buf, 1);
    close(f);
end.
