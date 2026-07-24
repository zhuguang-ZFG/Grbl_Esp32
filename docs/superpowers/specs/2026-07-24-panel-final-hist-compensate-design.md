# Panel Step8 hist 补偿对位（绝对停点）— 已撤销

日期：2026-07-24  
状态：**HIL 否定，已撤销**（2026-07-24 晚）

## 结论

用户要的「到位」是**相对当页纸边**再走 `PANEL_FINAL`，不是电机自 Step6 起的固定总行程。

hist 补偿在 `edge > hist`（晚采）时令 `remain < FINAL` → 相对纸边少走 → **经常不到位**。与写字对位目标冲突。

## 现行策略

- Step6：hist 仍冻结，仅服务盲走快进
- Step8：固定 `edge + PANEL_FINAL`（纸边相对）
- 低电流保持保留；`PANEL_FINAL_STEPS=300`
- 页间残余抖：靠采边质量 / 机械，不靠绝对行程补偿
