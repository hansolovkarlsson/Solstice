program TestClassArrayfieldMulti;
type
    TBox = class
        items: array[0..2] of integer;
        tag: integer;
    end;
var
    a, b: TBox;
begin
    new(a);
    new(b);

    a.items[0] := 1;
    a.items[1] := 2;
    a.items[2] := 3;
    a.tag := 111;

    b.items[0] := 9;
    b.items[1] := 8;
    b.items[2] := 7;
    b.tag := 222;

    { mutating b's array must not affect a's, and neither must disturb
      the OTHER scalar field ('tag') sitting right after the array in
      the same heap block - proves per-instance sizing/offset
      correctness across the array-plus-scalar layout. }
    writeln('a: ', a.items[0], ' ', a.items[1], ' ', a.items[2], ' tag=', a.tag);
    writeln('b: ', b.items[0], ' ', b.items[1], ' ', b.items[2], ' tag=', b.tag);

    b.items[1] := 999;
    writeln('a unchanged: ', a.items[0], ' ', a.items[1], ' ', a.items[2], ' tag=', a.tag);
    writeln('b changed: ', b.items[0], ' ', b.items[1], ' ', b.items[2], ' tag=', b.tag);

    dispose(a);
    dispose(b);
end.
