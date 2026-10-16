; 原生前端通过内核事件接收真实进度；直接运行内核时保留原向导。
[Code]
var
  QmNativeWindow: HWND;
  QmNativeProgress: Integer;

procedure InitializeWizard;
begin
  QmNativeWindow := StrToInt64Def(ExpandConstant('{param:QMNATIVEHWND|0}'), 0);
  QmNativeProgress := -1;
  if QmNativeWindow <> 0 then
    PostMessage(QmNativeWindow, $801F, WizardForm.Handle, 0);
end;

procedure CurInstallProgressChanged(CurProgress, MaxProgress: Integer);
var
  Progress: Integer;
begin
  if (QmNativeWindow <> 0) and (MaxProgress > 0) then
  begin
    Progress := (Int64(CurProgress) * 1000) div MaxProgress;
    if Progress <> QmNativeProgress then
    begin
      QmNativeProgress := Progress;
      PostMessage(QmNativeWindow, $8020, Progress, 1000);
    end;
  end;
end;

procedure CancelButtonClick(CurPageID: Integer; var Cancel, Confirm: Boolean);
begin
  if QmNativeWindow <> 0 then
    Confirm := False;
end;
