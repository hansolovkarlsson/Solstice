program TestWithBadUndeclared;
begin
    with nothere do
        writeln('hi');
end.
