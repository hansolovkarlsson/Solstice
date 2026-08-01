program TestGotoFunctionExit;

function Classify(x: integer): integer;
label 1;
begin
    if x < 0 then begin
        Classify := -1;
        goto 1;
    end;
    Classify := 1;
    1: writeln('classified');
end;

begin
    writeln(Classify(-5));
    writeln(Classify(5));
end.
