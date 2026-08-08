program TestRecordVariantMulti;
type
    TThing = record
        id: integer;
        case kind: integer of
            0, 1: (a, b: integer);
            2: ();
    end;
var
    t: TThing;
begin
    t.id := 42;
    t.kind := 0;
    t.a := 10;
    t.b := 20;
    writeln('id: ', t.id);
    writeln('kind: ', t.kind);
    writeln('a: ', t.a);
    writeln('b: ', t.b);

    t.kind := 1;
    t.a := 100;
    t.b := 200;
    writeln('kind: ', t.kind, ' a: ', t.a, ' b: ', t.b);

    t.kind := 2;
    writeln('kind after empty variant: ', t.kind);
end.
