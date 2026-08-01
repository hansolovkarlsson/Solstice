program TestGotoBadScope;
label 2;

procedure P;
label 1;
begin
    1: writeln('in P');
end;

begin
    goto 1;
    2: writeln('done');
end.
