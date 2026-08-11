program TestWarningInProc;

procedure Check(n: integer);
begin
    if n > 10 then
        warning('n is large');
    writeln('checked ', n);
end;

begin
    Check(3);
    Check(20);
end.
