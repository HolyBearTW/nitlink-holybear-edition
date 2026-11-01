# Xbox 登入設定（開發者）

NitLink 的 Microsoft 應用程式 Client ID 集中定義於 `src/app/account_service.h` 的 `XboxClientId` 常數。需要更換應用程式時，修改這一處並重新編譯即可；搜尋 `XboxClientId` 可找到授權、PKCE 換票及權杖更新的使用位置。

Client ID 是公開應用程式識別碼，不是使用者帳號資料或 Client Secret。使用者介面沒有編輯入口，連動資料檔也不會覆寫這個常數。已儲存的帳號憑證與遊戲紀錄仍從原本的 Windows 使用者加密檔載入。

應用程式維護者須在 Microsoft Entra 的「應用程式註冊 → 驗證 → 新增平台 → 行動及桌面應用程式」登記 `http://localhost`，並確保支援個人 Microsoft 帳號。NitLink 使用系統瀏覽器、動態本機連接埠與 PKCE，不需要 Client Secret；Microsoft 比對 localhost 回呼時會忽略連接埠。[Microsoft 回呼網址說明](https://learn.microsoft.com/entra/identity-platform/reply-url#localhost-exceptions)

一般使用者只需按「開啟登入頁」並完成 Microsoft 登入，NitLink 會接收回呼並同步 Xbox 遊戲資料。

人工驗證項目包括：完整登入與同步、登入取消、五分鐘逾時、重複回呼、其他程式占用連接埠，以及等待登入時關閉 NitLink。這份開發文件不隨一般使用者測試包發送；本機實際執行的檢查記錄於 `BUILD.txt`。
