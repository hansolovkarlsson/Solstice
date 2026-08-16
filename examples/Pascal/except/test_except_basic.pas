program TestExceptBasic;

// A raise inside a try, caught by its own except - confirms the basic
// mechanism and ExceptMessage reading the raised message back.

begin
  try
    writeln('before raise');
    raise 'something went wrong';
    writeln('unreachable');
  except
    writeln('caught: ', ExceptMessage);
  end;
  writeln('after try');
end.
