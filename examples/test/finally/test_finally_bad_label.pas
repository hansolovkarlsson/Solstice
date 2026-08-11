program TestFinallyBadLabel;
label 1;
begin
    try
        writeln('body');
    finally
        1: writeln('cleanup');
    end;
end.
