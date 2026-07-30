program IfTest;
var
    x: integer;
    label_result: boolean;
begin
    x := 7;
    if x > 5 then
        writeln(1)
    else
        writeln(0);

    if x < 5 then
        writeln(99);

    label_result := x = 7;
    if label_result then begin
        writeln(100);
        writeln(200);
    end else begin
        writeln(300);
    end;
end.
