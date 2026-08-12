program TestRandomDeterminism;
begin
    { No Randomize call - real Pascal's own convention is that Random's
      sequence is then deterministic, the same every run. Verify by
      compiling this once and running the resulting .bin more than
      once: both calls below must print the exact same two numbers on
      every run. }
    writeln(Random(1000000));
    writeln(Random(1000000));
end.
