# UART FIFO development evidence / UART FIFO 开发证物

Archive SHA256: `f95ebfc84139dcb2ee0b14996e108e2721c913eae6156f530ad4e7f931f0dce2`. Complete-member verification: 275 files / 4964559 bytes; see `package.json` and `verify.json`.

The archive preserves actual focused RED/GREEN, two fresh strict C11 ARM builds, original failures, source snapshots and dependency checks. Read `payload/SCOPE.md` for exact boundaries. The final ordinary commit gate and exact-head CI provide separate full integration evidence. No physical HIL or complete software candidate is claimed.

归档已逐成员验证，保留真实原始失败、源码与构建回执。各次测试绑定各自的源码范围；最终整轮门禁及 CI 单独验证最终提交。实板 HIL 未执行。

Verify after transport with the pinned Nexus source SDK:

```sh
python -B external/nexus/tools/evidence/store.py verify --package evidence/io-closure-20261020/package.json --archive evidence/io-closure-20261020/f95ebfc84139dcb2ee0b14996e108e2721c913eae6156f530ad4e7f931f0dce2.tar.gz --report build/io-evidence-verify.json
```
