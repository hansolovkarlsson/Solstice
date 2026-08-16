program TestCaseBasic;
const
    Bias = 10;
var
    x: integer;
begin
    for x := 1 to 5 do begin
        case x of
            1: writeln('one');
            2, 3: writeln('two or three');
        else
            writeln('other: ', x);
        end;
    end;

    { the selector can be any ordinal expression, not just a bare
      variable - and a case label can be an integer literal (optionally
      negative) or a 'const' reference }
    x := 5;
    case x + Bias of
        -5: writeln('negative five');
        15: writeln('fifteen');
    else
        writeln('unexpected');
    end;
end.
