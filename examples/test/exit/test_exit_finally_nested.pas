program TestExitFinallyNested;
{ 'exit;' from inside doubly-nested try/finally runs BOTH finally blocks,
  innermost first. Expected output: inner-try, inner-finally,
  outer-finally, done. }
procedure Foo;
begin
    try
        try
            writeln('inner-try');
            exit;
        finally
            writeln('inner-finally');
        end;
    finally
        writeln('outer-finally');
    end;
end;
begin
    Foo;
    writeln('done');
end.
