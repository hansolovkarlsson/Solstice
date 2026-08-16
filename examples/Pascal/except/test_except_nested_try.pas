program TestExceptNestedTry;

// A try inside another try's body - the inner try catches its own
// raise; the outer handler must never see it.

begin
  try
    writeln('outer try start');
    try
      writeln('inner try start');
      raise 'inner error';
      writeln('unreachable inner');
    except
      writeln('inner caught: ', ExceptMessage);
    end;
    writeln('outer try continues after inner catch');
  except
    writeln('outer caught - THIS IS WRONG');
  end;
  writeln('done');
end.
