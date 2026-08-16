program TestWarningExpr;
var
    reason: string;
    c: char;
begin
    reason := 'low ' + 'memory';
    warning(reason);
    c := 'X';
    warning(c);
end.
