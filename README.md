# my-mikanOS
本リポジトリは、『ゼロからのOS自作入門』（内田公太 (2021)、マイナビ出版）を参考にした自作 OS を拡張したものを載せています。
現在進行形で拡張していますがペースは遅め。


# 拡張内容
## Hugepage の実装
はじめに、オリジナルとの差分はこちら。(正確にはオリジナルでは無く、hugepage 実装途中のコミットとの差分になってます。そのため昔の未完成 hugepage 処理が見えちゃってます。）
[diff_hugepage](https://github.com/kkk1731/my-mikanOS/commit/bd641a88ddd68bfc7f88b2a90083a89b1a58a4fb#diff-0d6c189bc4fc1bce5a92f744d27cae3313d2f264c04e202c78941e7c33eade9f)
### 概要
mikanOS では物理メモリの管理を 4KiB ごとに bitmap で行っています。また、メモリのアロケート・ディアロケートを基本的に 1 ページずつ走査して行っています。demand paging なんかも 4KiB ずつやっていますが、大きいサイズを一度に割り当てて高速化させたかったので Hugepage を実装しました。

最終的に Hugepage を透過的に割り当てることをめざし、メモリ管理の粒度を複数種類にします。Linux のバディアロケータは 4MiB までの粒度で細かく管理していますが、まずは 2MiB の粒度だけ追加しています。

### 結果
結果の比較として、適当なアプリの実行時間を比較します。Hugepage を実装したものとしていないものを比較しています。（アプリのファイル内容は全く同じ、カーネルだけを変更)
- アプリ内容：ファイルを読み込み、コマンドで渡した引数(char)がそのファイル内に何回出現するか調べる。
- ファイルサイズ：約 3.4 MB

結果は、実行時間が約 1/4 になりました。10 回ずつ実行して平均時間を計算してもちょうど 1/4 くらいでした。喜び。こんな早くなった原因としては、TLB や PTwalk がどうこうというよりもページフォールト回数が減ったというのが大きいと思います。動作確認として dpaging_huge の背景画面に Hugepage 割り当てが成功したメッセージを出しています (kernel/pagin.cpp NewHugePageMap() 関数参照)。

改造前(左) と改造後(右) の実行時間(elapsed ...ms の行)
<p>
  <img src="https://user-images.githubusercontent.com/73451469/213874622-9c6dd258-39ff-4f27-bd58-30703624080c.png"　alt="Regular page" width=500>
  <img src="https://user-images.githubusercontent.com/73451469/213874552-eb1b1bf0-5ec7-4666-ac92-8cca86829ce5.png" alt="Hugepage" width=500>
</p>


## アクティブウィンドウ切り替え
hugepage 実装からの差分はこちら。
[diff_active_window](https://github.com/kkk1731/my-mikanOS/commit/cb106f69f6c978a4db369e049489b6d39152eabe)
### 概要
ウィンドウをクリックするとそのウィンドウがアクティブになり、
- 上部バーが明るくなる
- コマンド入力などの操作を受け付ける
- ウィンドウが最前面に来る(マウスカーソルを除く)

という機能があります。
それをコマンド（Alt + t）でもできるようにしました。
簡単そうですが複数クラスを変えなきゃいけなかったりして開発って大変だなと思いました。
