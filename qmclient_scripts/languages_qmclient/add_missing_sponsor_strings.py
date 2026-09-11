"""把赞助提醒相关新增字符串的真实译文写入 i18n TOML 存储。

用法：py -3 add_missing_sponsor_strings.py
幂等：按 key 定位 [message.translations] 块，逐语言覆盖为下表译文。
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import i18n_store  # noqa: E402

TOML_PATH = i18n_store.TRANSLATIONS_DIR / "qmclient.toml"

# key -> {language: translation}。中文为正式译文，其余语言按各自语言撰写。
CATALOG: dict[str, dict[str, str]] = {
    "Still free after %d sponsors. Take a look?": {
        "simplified_chinese": "已经靠 %d 位赞助者撑到现在，要看看吗？",
        "traditional_chinese": "已經靠 %d 位贊助者撐到現在，要看看嗎？",
        "japanese": "%d 人のスポンサーに支えられています。見てみますか？",
        "korean": "%d명의 후원자 덕분입니다. 보시겠어요?",
        "russian": "Нас держат %d спонсоров. Посмотрите?",
        "german": "Getragen von %d Unterstützern. Mal ansehen?",
        "spanish": "Nos sostienen %d patrocinadores. ¿Lo ves?",
        "french": "Grâce à %d sponsors. Aller voir ?",
        "brazilian_portuguese": "Sustentado por %d apoiadores. Quer ver?",
        "portuguese": "Sustentado por %d apoiadores. Quer ver?",
        "turkish": "Bizi %d destekçi ayakta tutuyor. Bakmak ister misin?",
        "polish": "Wspiera nas %d sponsorów. Zobaczysz?",
    },
    "Really? Not even a little?": {
        "simplified_chinese": "真的？一点都不考虑一下吗？",
        "traditional_chinese": "真的？一點都不考慮一下嗎？",
        "japanese": "本当に？少しも考えてくれないんですか？",
        "korean": "정말요? 조금도 생각해 보실 생각이 없나요?",
        "russian": "Серьёзно? Даже чуть-чуть?",
        "german": "Wirklich? Nicht mal ein bisschen?",
        "spanish": "¿En serio? ¿Ni un poquito?",
        "french": "Vraiment ? Même pas un peu ?",
        "brazilian_portuguese": "Sério? Nem um pouquinho?",
        "portuguese": "A sério? Nem um pouquinho?",
        "turkish": "Gerçekten mi? Hiç mi düşünmezsin?",
        "polish": "Serio? Ani trochę?",
    },
    "Sponsor reminder": {
        "simplified_chinese": "赞助提醒",
        "traditional_chinese": "贊助提醒",
        "japanese": "スポンサー通知",
        "korean": "후원 알림",
        "russian": "Напоминание о спонсорстве",
        "german": "Unterstützer-Hinweis",
        "spanish": "Recordatorio de patrocinio",
        "french": "Rappel de sponsoring",
        "brazilian_portuguese": "Lembrete de apoio",
        "portuguese": "Lembrete de apoio",
        "turkish": "Destekçi hatırlatması",
        "polish": "Przypomnienie o wsparciu",
    },
    "Sponsor reminder preview": {
        "simplified_chinese": "赞助提醒预览",
        "traditional_chinese": "贊助提醒預覽",
        "japanese": "スポンサー通知のプレビュー",
        "korean": "후원 알림 미리보기",
        "russian": "Предпросмотр напоминания",
        "german": "Vorschau des Unterstützer-Hinweises",
        "spanish": "Vista previa del recordatorio",
        "french": "Aperçu du rappel de sponsoring",
        "brazilian_portuguese": "Prévia do lembrete de apoio",
        "portuguese": "Pré-visualização do lembrete",
        "turkish": "Destekçi hatırlatması önizlemesi",
        "polish": "Podgląd przypomnienia o wsparciu",
    },
    "Accumulated client launch count (used by the sponsor reminder)": {
        "simplified_chinese": "累计启动次数（用于赞助提醒）",
        "traditional_chinese": "累計啟動次數（用於贊助提醒）",
        "japanese": "クライアントの累計起動回数（スポンサー通知に使用）",
        "korean": "누적 클라이언트 실행 횟수(후원 알림에 사용)",
        "russian": "Суммарное число запусков клиента (для напоминания о спонсорстве)",
        "german": "Kumulierte Client-Starts (für den Unterstützer-Hinweis)",
        "spanish": "Número acumulado de inicios del cliente (para el recordatorio)",
        "french": "Nombre cumulé de lancements du client (pour le rappel)",
        "brazilian_portuguese": "Contagem acumulada de execuções (para o lembrete)",
        "portuguese": "Contagem acumulada de arranques (para o lembrete)",
        "turkish": "Toplam istemci başlatma sayısı (destekçi hatırlatması için)",
        "polish": "Łączna liczba uruchomień klienta (dla przypomnienia)",
    },
    "Launch count threshold for the next sponsor reminder": {
        "simplified_chinese": "下次赞助提醒的启动次数阈值",
        "traditional_chinese": "下次贊助提醒的啟動次數閾值",
        "japanese": "次のスポンサー通知までの起動回数しきい値",
        "korean": "다음 후원 알림까지의 실행 횟수 임계값",
        "russian": "Порог числа запусков до следующего напоминания",
        "german": "Startschwelle für den nächsten Unterstützer-Hinweis",
        "spanish": "Umbral de inicios para el próximo recordatorio",
        "french": "Seuil de lancements avant le prochain rappel",
        "brazilian_portuguese": "Limite de execuções para o próximo lembrete",
        "portuguese": "Limite de arranques para o próximo lembrete",
        "turkish": "Sonraki hatırlatma için başlatma eşiği",
        "polish": "Próg uruchomień do następnego przypomnienia",
    },
    "Show the occasional sponsor reminder in the main menu": {
        "simplified_chinese": "在主菜单偶尔显示赞助提醒",
        "traditional_chinese": "在主選單偶爾顯示贊助提醒",
        "japanese": "メインメニューでスポンサー通知を時々表示する",
        "korean": "메인 메뉴에서 후원 알림을 가끔 표시",
        "russian": "Иногда показывать напоминание о спонсорстве в главном меню",
        "german": "Gelegentlichen Unterstützer-Hinweis im Hauptmenü zeigen",
        "spanish": "Mostrar ocasionalmente el recordatorio en el menú principal",
        "french": "Afficher occasionnellement le rappel dans le menu principal",
        "brazilian_portuguese": "Mostrar ocasionalmente o lembrete no menu principal",
        "portuguese": "Mostrar ocasionalmente o lembrete no menu principal",
        "turkish": "Ana menüde ara sıra destekçi hatırlatması göster",
        "polish": "Pokazuj czasem przypomnienie w menu głównym",
    },
    "Preview the sponsor reminder without changing the launch count": {
        "simplified_chinese": "预览赞助提醒，不影响启动计数",
        "traditional_chinese": "預覽贊助提醒，不影響啟動計數",
        "japanese": "起動回数を変えずにスポンサー通知をプレビュー",
        "korean": "실행 횟수를 바꾸지 않고 후원 알림 미리보기",
        "russian": "Предпросмотр напоминания без изменения счётчика запусков",
        "german": "Unterstützer-Hinweis ohne Änderung der Startzahl ansehen",
        "spanish": "Previsualizar el recordatorio sin cambiar el contador",
        "french": "Prévisualiser le rappel sans modifier le compteur",
        "brazilian_portuguese": "Pré-visualizar o lembrete sem alterar a contagem",
        "portuguese": "Pré-visualizar o lembrete sem alterar a contagem",
        "turkish": "Başlatma sayısını değiştirmeden hatırlatmayı önizle",
        "polish": "Podejrzyj przypomnienie bez zmiany licznika uruchomień",
    },
}


def replace_block(text: str, key: str, translations: dict[str, str]) -> tuple[str, bool]:
    """把指定 key 的 [message.translations] 块内语言行替换为给定译文。"""
    # 注意两点：
    # 1) toml_quote 已经产出带引号的字面量，模板里不能再包一层引号；
    # 2) 不能直接用 re.escape：Python 3.12 起它会把空格转义成 "\ "，导致字面量匹配失败。
    #    这里只转义真正的正则元字符，保留空格原样。
    quoted = i18n_store.toml_quote(key)
    quoted = re.sub(r'([.^$*+?()\[\]{}|\\])', r"\\\1", quoted)
    pattern = re.compile(
        r"(?ms)(^\[\[message\]\]\nkey = " + quoted + r"\n\[message\.translations\]\n)(.*?)(?=^\[\[message\]\]|\Z)"
    )
    match = pattern.search(text)
    if match is None:
        return text, False

    body_lines = []
    for lang, value in translations.items():
        body_lines.append(f"{lang} = {i18n_store.toml_quote(value)}")
    new_block = match.group(1) + "\n".join(body_lines) + "\n"
    return text[: match.start()] + new_block + text[match.end() :], True


def main() -> None:
    text = TOML_PATH.read_text(encoding="utf-8")
    updated = 0
    missing = []
    for key, translations in CATALOG.items():
        text, ok = replace_block(text, key, translations)
        if ok:
            updated += 1
            print(f"  译文写入: {key}")
        else:
            missing.append(key)

    TOML_PATH.write_text(text, encoding="utf-8", newline="\n")
    print(f"完成：updated={updated}, not_found={len(missing)}")
    for key in missing:
        print(f"  未找到该 key（需先插入条目）: {key}")


if __name__ == "__main__":
    main()
