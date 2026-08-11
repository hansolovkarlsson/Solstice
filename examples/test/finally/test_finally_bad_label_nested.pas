program TestFinallyBadLabelNested;
label 1;
var i: integer;
begin
    try
        writeln('body');
    finally
        i := 0;
        while i < 3 do begin
            i := i + 1;
            1: writeln(i);
        end;
    end;
end.
