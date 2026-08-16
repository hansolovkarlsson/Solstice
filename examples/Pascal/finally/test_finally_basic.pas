program TestFinallyBasic;
begin
    try
        writeln('body');
    finally
        writeln('cleanup');
    end;
    writeln('after');
end.
