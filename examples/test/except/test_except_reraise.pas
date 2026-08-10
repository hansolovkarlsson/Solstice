program TestExceptReraise;

// A raise from inside an except-body itself, caught by an ENCLOSING
// try - proves the handler that just fired is correctly consumed
// (popped) before jumping to it, so the re-raise finds the next
// handler out rather than looping back into itself.

begin
  try
    try
      raise 'original error';
    except
      writeln('inner except: ', ExceptMessage);
      raise 'rethrown error';
    end;
    writeln('unreachable after inner try');
  except
    writeln('outer caught: ', ExceptMessage);
  end;
  writeln('done');
end.
