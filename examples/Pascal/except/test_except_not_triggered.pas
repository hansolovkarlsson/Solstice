program TestExceptNotTriggered;

// The try-body completes normally (no raise) - the except-body must
// NOT execute. Proves OP_END_TRY correctly deactivates the handler and
// jumps past the except-body.

begin
  try
    writeln('try body ran');
  except
    writeln('except body ran - THIS IS WRONG');
  end;
  writeln('after try');
end.
