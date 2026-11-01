# 主機帳號登入：開發與驗收

Nintendo 與 Sony 的自動登入沿用 `account_service.cpp` 現有的權杖交換及遊戲同步。以下是 NitLink 的桌面整合方式，不表示平台已提供 NitLink 專用的公開第三方授權資格。Xbox 的公開 Client ID 與維護設定見 [XBOX-AUTH.md](XBOX-AUTH.md)。

## Nintendo

`nintendo_auth_callback.cpp` 在目前 Windows 使用者的 `Software\Classes\npf5c38e31cd085304b` 暫時註冊登入協定。既有入口屬於其他程式，或另一個 NitLink 登入接收器仍在使用時，會回報可改用手動連動，不覆寫該入口。

瀏覽器回呼會啟動同一執行檔的 `--nintendo-auth-return` 模式，經 `WM_COPYDATA` 將回呼送回此次接收器。這個輔助模式在 COM／Media Foundation／擷取初始化之前結束，不會啟動第二個擷取視窗。接收器只接受正確協定、唯一且相符的 `state` 與單次授權碼；PKCE 換票仍由帳號服務執行。取消、五分鐘逾時、成功或程式關閉都會釋放此程式建立的入口。

此作法可參考 [nxapi 的系統瀏覽器回呼實作](https://github.com/samuelthomas2774/nxapi/blob/main/src/app/main/na-auth.ts)及 [Windows WM_COPYDATA](https://learn.microsoft.com/windows/win32/dataxchg/wm-copydata)。NitLink 沒有引入 nxapi 或改為 Nintendo Switch Online／Coral 的資料介面；遊玩資料仍使用既有 Play Activity 來源。

## Sony PSN

`psn_auth_browser.cpp` 優先選用 Windows 預設的 Edge／Chrome；預設程式不支援此方式時，再尋找已安裝的 Edge／Chrome。程式使用 `%LOCALAPPDATA%\NitLink\AccountLogin\PlayStation\{GUID}` 新建的獨立資料夾開啟官方登入頁。選擇預設瀏覽器程式不代表沿用其日常登入資料；此模式不連接日常瀏覽器或 NitLink 設定頁的 WebView2。若要沿用日常瀏覽器的 Sony 登入狀態，可使用手動 NPSSO／JSON 入口。

專用瀏覽器使用動態本機除錯埠；程式只連線至 `127.0.0.1`，從該資料夾的 `DevToolsActivePort` 取得此次端點，再透過 DevTools Protocol 查詢 Cookie，僅將 Sony 網域的 `npsso` 交給既有換票流程。沒有注入登入腳本、讀取密碼或擴張設定頁的 WebView 權限。瀏覽器由專用 Job Object 管理；取消、逾時、成功或程式關閉時，結束該次瀏覽器並清理其資料夾。

此方向已有 [psn-mcp 的原始實作](https://github.com/tmrp/psn-mcp/blob/main/src/psn/browser-login.ts)；瀏覽器隔離限制也可見 [Chrome 的除錯埠變更說明](https://developer.chrome.com/blog/remote-debugging-port)。網站政策或企業瀏覽器設定仍可能拒絕自動模式，因此手動 NPSSO／JSON 連動入口保留。

## 人工驗收

- Nintendo：在系統瀏覽器登入並選擇帳號，允許開啟 NitLink；確認自動同步、沒有第二個擷取視窗、再次送回同一回呼不重複換票，以及結束後回呼入口已釋放。
- Sony：在專用視窗完成登入及 MFA／Passkey，確認自動同步，結束後只有此次專用瀏覽器被關閉；另確認關閉登入視窗、取消與五分鐘逾時。
- Nintendo／Sony：驗證手動入口、取消後重試，以及既有帳號的重新同步。來源仍為自動時，依已連動平台的遊戲數量選擇預設來源；啟動讀入的既有遊戲庫與首次同步都適用，手動指定的來源保留。數量相同時依 Nintendo、PlayStation、Xbox 的固定順序選擇，全部為零時維持原設定；選定後只提示一次，三秒後消失。
- Nintendo：回呼的 `auth` 與 `auth/` 形式都應完成連動；兩者都必須驗證此次 `state`。重新連動既有帳號時，介面應等這次換票與同步成功後才關閉，不能因舊的已連動狀態提前關閉。
- Xbox：確認現有自動登入仍可完成；沒有恢復手動回呼或 Client ID 設定入口。

授權碼、NPSSO、權杖與完整敏感回呼網址不寫入日誌。連動資料存入原本的 Windows 使用者加密檔；專用瀏覽器資料夾、個人設定及連動憑證不列入發送包。此輪依使用者要求只進行編譯與靜態／包檔檢查，不執行 CTest 或回歸測試；實際狀態記錄於 `BUILD.txt`。
