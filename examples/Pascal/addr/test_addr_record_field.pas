program TestAddrRecordField;

{ @(p^.field) for a multi-field record - the nonzero-offset case.
  Writing through the cast-back pointer mutates the SAME storage the
  original record field reads from, confirming this is a real address,
  not a copy. 42 99 }

type
    PInt = ^integer;
    PPoint = ^TPoint;
    TPoint = record
        x, y: integer;
    end;

var
    p: PPoint;
    g: Pointer;
    fieldPtr: PInt;

begin
    new(p);
    p^.x := 10;
    p^.y := 42;

    g := @(p^.y);
    fieldPtr := PInt(g);
    writeln(fieldPtr^);

    fieldPtr^ := 99;
    writeln(p^.y);
end.
