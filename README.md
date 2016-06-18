RoundTripOpus
=============

RoundTripOpusは入力音声を非可逆音声圧縮形式[Opus](http://opus-codec.org/)でエンコード・デコードする
VST/AudioUnitエフェクトです。

RoundTripOpus is the VST/AudioUnit effect which encodes and decodes the audio input using the
lossy audio codec called [Opus](http://opus-codec.org/).

VST/AudioUnit対応ソフトウェアとともに用いると、音声をOpusで圧縮した際にどの程度劣化するのかを
調べることができます。これ以外にも、エフェクタとして使用できる可能性はありますが、安定性
(1日で書いたのであまりテストしていません)や処理遅延を考えるとあまりお勧めできる使用法ではありません。

本エフェクトはあまりテストされていませんので、ホストアプリケーションがクラッシュしても大丈夫なよう
使用前にプロジェクトを保存するなどして下さい。

オプション
----------

* **Sampling Rate** エンコードする際に使用するサンプリングレートを指定します。(Opusが使用できるサンプリングレートは限られています。)
* **Bit Rate** ターゲットとなるビットレートをbpsで指定します。
* **Frame Size** エンコードを行う単位をミリ秒で指定します。

### Audio Unitsでの注意点

Audio Unitsでは、値が0〜1の範囲にスケーリングされて表示されます。各項目の実際の範囲は以下の通りです。

* **Sampling Rate** 8000 〜 48000 [Hz] で、800, 12000, 16000, 24000, 48000のいずれかに丸め込まれる
* **Bit Rate** 600 〜 512000 [bps]
* **Frame Size** 2.5 〜 60 [ms]


ライセンス
----------

GPLv3

ビルド方法
----------

[JUCE](http://www.juce.com/)をダウンロードしてアレして下さい。

以下のライブラリが必要となります。

* [libopus](http://opus-codec.org/downloads/)
* [Secret Rabbit Code](http://www.mega-nerd.com/SRC/) a.k.a. libsamplerate

インストール方法
----------------

### Mac OS X

- Audio Unit: `RoundTripOpus.component` を `/Users/ユーザ名/Library/Audio/Plug-Ins/Components` にコピーして下さい。
- VST:  `RoundTripOpus.vst` を `/Users/ユーザ名/Library/Audio/Plug-Ins/VST` にコピーして下さい。
