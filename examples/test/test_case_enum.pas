program TestCaseEnum;
type
    TColor = (Red, Green, Blue);
var
    c: TColor;
begin
    c := Green;
    case c of
        Red: writeln('red');
        Green: writeln('green');
        Blue: writeln('blue');
    end;

    { boolean selector, also ordinal - every value covered, no else needed }
    case true of
        true: writeln('yes');
        false: writeln('no');
    end;
end.
