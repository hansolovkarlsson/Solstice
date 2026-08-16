program TestFinallyUncaught;
begin
    { no enclosing 'except' anywhere - cleanup must still run (via the
      exception-unwind copy) before the re-raise ultimately hits the
      fatal 'Unhandled exception' path }
    try
        raise 'nobody catches this';
    finally
        writeln('cleanup still runs');
    end;
    writeln('never reached');
end.
