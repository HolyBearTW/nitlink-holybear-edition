# NitLink 1.2.6 HolyBear Edition

**非官方社群整合版（Unofficial Community Edition）**

本版同步上游 NitLink 1.2.6 的新增功能，保留 HolyBear Edition 的遊戲資料庫、封面編輯、Discord、音訊與玻璃視窗整合。版本與「關於」頁的來源說明統一更新為 1.2.6。

## 本次更新

- F1 新增「遊戲中隱藏電腦游標」，預設關閉，可儲存設定。啟用時，游標進入作用中 NitLink 視窗或全螢幕的遊戲畫面即隱藏；標題列、設定介面、子母畫面、無訊號與其他應用程式仍顯示游標。擷取影像內的遊戲游標不受影響。
- 新增完整土耳其語，可選擇 Türkçe，並支援土耳其語 Windows 自動選取。除了上游選單，也補齊 HolyBear 的遊戲、封面、帳號、Discord、更新、快捷鍵、原生提示與 HUD，並在「關於」頁加入翻譯者資訊卡。
- 檢查全部七種語言的字串與替換參數。WinUI 安裝／更新／移除介面新增土耳其語；傳統安裝器補齊德文、日文、韓文與土耳其語，並保留安裝器前後端的語言選擇。修正德文商店名稱及各語言的 Buy Me a Coffee 支持連結文字。
- 設定頁、HUD、原生更新檢查、Windows 檔案資訊、安裝器及來源紀錄統一升級為 1.2.6-holybear，基礎版本說明改為 NitLink 1.2.6。
- 建置與封裝遷移至 Visual Studio 2026，正式封裝要求 Stable 工具鏈及 Microsoft 簽章的 VC145 runtime；CI 使用明確的 VS 2026 runner。

## 相容性與驗證

- 沿用既有 HolyBear 的 HDR／VRR、擷取裝置、音訊預設與獨立設定視窗行為。
- 既有 GC553Pro／Switch 2 流程先前已完成真實實機驗收；本版未重新進行該實機驗收，游標與語系變更以本次自動化結果為準。
- 發版驗證涵蓋完整 CTest、七個選單語系、設定與 WebView 必要檢查、簽章、封裝完整性、隱私及隔離安裝／升級／移除流程；實際結果列於發版驗證紀錄。

## 已知限制

- 本次同步範圍為 [GitHub 開源版 1.2.6](https://github.com/nitlink-dev/nitlink/releases/tag/v1.2.6)；上游 Steam 版的專屬補幀功能不包含在開源來源中。
- 自動化驗證不代表本次已重新進行所有擷取卡與顯示器的實機測試。
- 當自動模式無法取得更新率時，仍使用保守的 30 Hz fallback；手動上限與關閉上限維持原有設定。

## 來源、授權與致謝

- 原始專案：@nitlink-dev，[nitlink-dev/nitlink](https://github.com/nitlink-dev/nitlink)；整合與維護：@HolyBearTW。
- 土耳其語翻譯：@karsiyakali1912，[上游 PR #48](https://github.com/nitlink-dev/nitlink/pull/48)；感謝 @SwankyT1ger 提出[隱藏游標需求 #41](https://github.com/nitlink-dev/nitlink/issues/41)。
- 保留 @CN115 的音訊來源、@tavricccc 的安裝器來源及其他貢獻者致謝；詳見 [來源紀錄](https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/HOLYBEAR-SOURCES.json)、[第三方授權](https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/LICENSES.md)與 [MIT 授權](https://github.com/HolyBearTW/nitlink-holybear-edition/blob/holybear/integrated/LICENSE)。
- 感謝 @n810K、@Noka123、@i0rph 及其他貢獻者，完整內容保留於「關於」頁與致謝文件。

## English Summary

Based on upstream NitLink 1.2.6, this release adds optional gameplay cursor hiding and complete Turkish localization, including HolyBear features, native HUD/toasts and both installer interfaces. All seven languages are checked for complete strings and matching placeholders. The classic installer now also supports German, Japanese and Korean. The cursor setting supports HolyBear's video child window, custom caption and independent settings window. Version identifiers and upstream source records now identify 1.2.6; release builds use Visual Studio 2026 Stable and the signed VC145 redistributable runtime.
