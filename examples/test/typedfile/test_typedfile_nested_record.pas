program TestTypedfileNested;
type
    TPoint = record
        x, y: integer;
    end;
    TShape = record
        origin: TPoint;
        radius: real;
    end;
var
    f: file of TShape;
    s, s2: TShape;
begin
    assign(f, '/tmp/ouroboros_test_typedfile_nested.bin');
    rewrite(f);
    s.origin.x := 10;
    s.origin.y := 20;
    s.radius := 5.5;
    write(f, s);
    close(f);

    reset(f);
    read(f, s2);
    writeln('origin.x=', s2.origin.x, ' origin.y=', s2.origin.y, ' radius=', s2.radius:0:2);
    close(f);
end.
