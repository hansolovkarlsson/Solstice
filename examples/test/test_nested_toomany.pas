program TestNestedTooMany;

{ 18 levels of nesting deliberately exceeds MAX_NESTING_DEPTH (16) -
  must fail with a clean compile_error, not a parser stack overflow/crash }
procedure Level0;
    procedure Level1;
        procedure Level2;
            procedure Level3;
                procedure Level4;
                    procedure Level5;
                        procedure Level6;
                            procedure Level7;
                                procedure Level8;
                                    procedure Level9;
                                        procedure Level10;
                                            procedure Level11;
                                                procedure Level12;
                                                    procedure Level13;
                                                        procedure Level14;
                                                            procedure Level15;
                                                                procedure Level16;
                                                                    procedure Level17;
                                                                    begin
                                                                    end;
                                                                begin
                                                                    Level17;
                                                                end;
                                                            begin
                                                                Level16;
                                                            end;
                                                        begin
                                                            Level15;
                                                        end;
                                                    begin
                                                        Level14;
                                                    end;
                                                begin
                                                    Level13;
                                                end;
                                            begin
                                                Level12;
                                            end;
                                        begin
                                            Level11;
                                        end;
                                    begin
                                        Level10;
                                    end;
                                begin
                                    Level9;
                                end;
                            begin
                                Level8;
                            end;
                        begin
                            Level7;
                        end;
                    begin
                        Level6;
                    end;
                begin
                    Level5;
                end;
            begin
                Level4;
            end;
        begin
            Level3;
        end;
    begin
        Level2;
    end;
begin
    Level1;
end;

begin
    Level0;
end.
