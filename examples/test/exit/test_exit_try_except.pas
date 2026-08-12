program TestExitTryExcept;
{ 'exit;' from inside a try/except's protected try-body (not its handler)
  must still balance OP_TRY/OP_END_TRY correctly - confirmed here by
  making a SECOND call to the same procedure that actually raises and is
  caught, right after the first call took the exit path, proving the
  first exit didn't leave a stale vm_except_stack entry behind. Expected
  output: exiting early, about to raise, caught, after try, done. }
procedure Foo(doRaise: boolean);
begin
    try
        if not doRaise then begin
            writeln('exiting early');
            exit;
        end;
        writeln('about to raise');
        raise 'boom';
    except
        writeln('caught');
    end;
    writeln('after try');
end;
begin
    Foo(false);
    Foo(true);
    writeln('done');
end.
