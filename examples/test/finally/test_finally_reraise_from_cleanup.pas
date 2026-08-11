program TestFinallyReraiseFromCleanup;
begin
    { an exception raised from inside the cleanup code itself supersedes
      the original exception being unwound }
    try
        try
            raise 'original';
        finally
            writeln('cleanup runs');
            raise 'from cleanup';
        end;
    except
        writeln('caught: ', ExceptMessage);
    end;
end.
