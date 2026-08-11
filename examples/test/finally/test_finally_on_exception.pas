program TestFinallyOnException;
begin
    { try/finally and try/except stay separate constructs - nest to get
      both, matching real Delphi }
    try
        try
            writeln('body');
            raise 'boom';
            writeln('never');
        finally
            writeln('cleanup');
        end;
    except
        writeln('caught: ', ExceptMessage);
    end;
    writeln('after');
end.
