program TestNestedRecord;
type
    TPoint = record
        x: integer;
        y: integer;
    end;

{ nested procedure reads and writes a field of Outer's own local record -
  a record local's fields are just ordinary per-field frame slots (see
  add_local_record() in parser.c), so this exercises the same
  OP_LOAD_ENCLOSING/OP_STORE_ENCLOSING path a plain scalar local does }
procedure Outer;
    var p: TPoint;

    procedure Translate(dx, dy: integer);
    begin
        p.x := p.x + dx;
        p.y := p.y + dy;
    end;

begin
    p.x := 1;
    p.y := 1;
    Translate(4, 9);
    writeln('x=', p.x, ' y=', p.y);
end;

begin
    Outer;
end.
