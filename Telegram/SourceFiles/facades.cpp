/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "profile/profile_section_memento.h"
#include "core/click_handler_types.h"
#include "media/media_clip_reader.h"
#include "observer_peer.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "messenger.h"
#include "boxes/confirm_box.h"
#include "layerwidget.h"
#include "lang.h"
#include "base/observer.h"
#include "base/task_queue.h"

Q_DECLARE_METATYPE(ClickHandlerPtr);
Q_DECLARE_METATYPE(Qt::MouseButton);
Q_DECLARE_METATYPE(Ui::ShowWay);

namespace App {
namespace internal {

void CallDelayed(int duration, base::lambda_once<void()> &&lambda) {
	QTimer::singleShot(duration, base::lambda_slot_once(App::app(), std::move(lambda)), SLOT(action()));
}

} // namespace internal

void sendBotCommand(PeerData *peer, UserData *bot, const QString &cmd, MsgId replyTo) {
	if (auto m = main()) {
		m->sendBotCommand(peer, bot, cmd, replyTo);
	}
}

void hideSingleUseKeyboard(const HistoryItem *msg) {
	if (auto m = main()) {
		m->hideSingleUseKeyboard(msg->history()->peer, msg->id);
	}
}

bool insertBotCommand(const QString &cmd) {
	if (auto m = main()) {
		return m->insertBotCommand(cmd);
	}
	return false;
}

void activateBotCommand(const HistoryItem *msg, int row, int col) {
	const HistoryMessageReplyMarkup::Button *button = nullptr;
	if (auto markup = msg->Get<HistoryMessageReplyMarkup>()) {
		if (row < markup->rows.size()) {
			auto &buttonRow = markup->rows[row];
			if (col < buttonRow.size()) {
				button = &buttonRow.at(col);
			}
		}
	}
	if (!button) return;

	using ButtonType = HistoryMessageReplyMarkup::Button::Type;
	switch (button->type) {
	case ButtonType::Default: {
		// Copy string before passing it to the sending method
		// because the original button can be destroyed inside.
		MsgId replyTo = (msg->id > 0) ? msg->id : 0;
		sendBotCommand(msg->history()->peer, msg->fromOriginal()->asUser(), QString(button->text), replyTo);
	} break;

	case ButtonType::Callback:
	case ButtonType::Game: {
		if (auto m = main()) {
			m->app_sendBotCallback(button, msg, row, col);
		}
	} break;

	case ButtonType::Buy: {
		Ui::show(Box<InformBox>(lang(lng_payments_not_supported)));
	} break;

	case ButtonType::Url: {
		auto url = QString::fromUtf8(button->data);
		auto skipConfirmation = false;
		if (auto bot = msg->getMessageBot()) {
			if (bot->isVerified()) {
				skipConfirmation = true;
			}
		}
		if (skipConfirmation) {
			UrlClickHandler::doOpen(url);
		} else {
			HiddenUrlClickHandler::doOpen(url);
		}
	} break;

	case ButtonType::RequestLocation: {
		hideSingleUseKeyboard(msg);
		Ui::show(Box<InformBox>(lang(lng_bot_share_location_unavailable)));
	} break;

	case ButtonType::RequestPhone: {
		hideSingleUseKeyboard(msg);
		Ui::show(Box<ConfirmBox>(lang(lng_bot_share_phone), lang(lng_bot_share_phone_confirm), [peerId = msg->history()->peer->id] {
			if (auto m = App::main()) {
				m->onShareContact(peerId, App::self());
			}
		}));
	} break;

	case ButtonType::SwitchInlineSame:
	case ButtonType::SwitchInline: {
		if (auto m = App::main()) {
			if (auto bot = msg->getMessageBot()) {
				auto tryFastSwitch = [bot, &button, msgId = msg->id]() -> bool {
					auto samePeer = (button->type == ButtonType::SwitchInlineSame);
					if (samePeer) {
						Notify::switchInlineBotButtonReceived(QString::fromUtf8(button->data), bot, msgId);
						return true;
					} else if (bot->botInfo && bot->botInfo->inlineReturnPeerId) {
						if (Notify::switchInlineBotButtonReceived(QString::fromUtf8(button->data))) {
							return true;
						}
					}
					return false;
				};
				if (!tryFastSwitch()) {
					m->inlineSwitchLayer('@' + bot->username + ' ' + QString::fromUtf8(button->data));
				}
			}
		}
	} break;
	}
}

void searchByHashtag(const QString &tag, PeerData *inPeer) {
	if (MainWidget *m = main()) m->searchMessages(tag + ' ', (inPeer && inPeer->isChannel() && !inPeer->isMegagroup()) ? inPeer : 0);
}

void openPeerByName(const QString &username, MsgId msgId, const QString &startToken) {
	if (MainWidget *m = main()) m->openPeerByName(username, msgId, startToken);
}

void joinGroupByHash(const QString &hash) {
	if (MainWidget *m = main()) m->joinGroupByHash(hash);
}

void stickersBox(const QString &name) {
	if (MainWidget *m = main()) m->stickersBox(MTP_inputStickerSetShortName(MTP_string(name)));
}

void openLocalUrl(const QString &url) {
	if (MainWidget *m = main()) m->openLocalUrl(url);
}

bool forward(const PeerId &peer, ForwardWhatMessages what) {
	if (MainWidget *m = main()) return m->onForward(peer, what);
	return false;
}

void removeDialog(History *history) {
	if (MainWidget *m = main()) {
		m->removeDialog(history);
	}
}

void showSettings() {
	if (auto w = wnd()) {
		w->showSettings();
	}
}

void activateClickHandler(ClickHandlerPtr handler, Qt::MouseButton button) {
	if (auto w = wnd()) {
		qRegisterMetaType<ClickHandlerPtr>();
		qRegisterMetaType<Qt::MouseButton>();
		QMetaObject::invokeMethod(w, "app_activateClickHandler", Qt::QueuedConnection, Q_ARG(ClickHandlerPtr, handler), Q_ARG(Qt::MouseButton, button));
	}
}

void logOutDelayed() {
	App::CallDelayed(1, App::app(), [] {
		App::logOut();
	});
}

} // namespace App

namespace Ui {
namespace internal {

void showBox(object_ptr<BoxContent> content, ShowLayerOptions options) {
	if (auto w = App::wnd()) {
		w->ui_showBox(std::move(content), options);
	}
}

} // namespace internal

void showMediaPreview(DocumentData *document) {
	if (auto w = App::wnd()) {
		w->ui_showMediaPreview(document);
	}
}

void showMediaPreview(PhotoData *photo) {
	if (auto w = App::wnd()) {
		w->ui_showMediaPreview(photo);
	}
}

void hideMediaPreview() {
	if (auto w = App::wnd()) {
		w->ui_hideMediaPreview();
	}
}

void hideLayer(bool fast) {
	if (auto w = App::wnd()) {
		w->ui_showBox({ nullptr }, CloseOtherLayers | (fast ? ForceFastShowLayer : AnimatedShowLayer));
	}
}

void hideSettingsAndLayer(bool fast) {
	if (auto w = App::wnd()) {
		w->ui_hideSettingsAndLayer(fast ? ForceFastShowLayer : AnimatedShowLayer);
	}
}

bool isLayerShown() {
	if (auto w = App::wnd()) return w->ui_isLayerShown();
	return false;
}

bool isMediaViewShown() {
	if (auto w = App::wnd()) return w->ui_isMediaViewShown();
	return false;
}

void repaintHistoryItem(const HistoryItem *item) {
	if (auto main = App::main()) {
		main->ui_repaintHistoryItem(item);
	}
}

void autoplayMediaInlineAsync(const FullMsgId &msgId) {
	if (auto main = App::main()) {
		QMetaObject::invokeMethod(main, "ui_autoplayMediaInlineAsync", Qt::QueuedConnection, Q_ARG(qint32, msgId.channel), Q_ARG(qint32, msgId.msg));
	}
}

void showPeerProfile(const PeerId &peer) {
	if (auto main = App::main()) {
		main->showWideSection(Profile::SectionMemento(App::peer(peer)));
	}
}

void showPeerOverview(const PeerId &peer, MediaOverviewType type) {
	if (auto m = App::main()) {
		m->showMediaOverview(App::peer(peer), type);
	}
}

void showPeerHistory(const PeerId &peer, MsgId msgId, ShowWay way) {
	if (MainWidget *m = App::main()) m->ui_showPeerHistory(peer, msgId, way);
}

void showPeerHistoryAsync(const PeerId &peer, MsgId msgId, ShowWay way) {
	if (MainWidget *m = App::main()) {
		qRegisterMetaType<Ui::ShowWay>();
		QMetaObject::invokeMethod(m, "ui_showPeerHistoryAsync", Qt::QueuedConnection, Q_ARG(quint64, peer), Q_ARG(qint32, msgId), Q_ARG(Ui::ShowWay, way));
	}
}

PeerData *getPeerForMouseAction() {
	if (auto w = App::wnd()) {
		return w->ui_getPeerForMouseAction();
	}
	return nullptr;
}

bool hideWindowNoQuit() {
	if (!App::quitting()) {
		if (auto w = App::wnd()) {
			return w->hideNoQuit();
		}
	}
	return false;
}

bool skipPaintEvent(QWidget *widget, QPaintEvent *event) {
	if (auto w = App::wnd()) {
		if (w->contentOverlapped(widget, event)) {
			return true;
		}
	}
	return false;
}

} // namespace Ui

namespace Notify {

void userIsBotChanged(UserData *user) {
	if (MainWidget *m = App::main()) m->notify_userIsBotChanged(user);
}

void userIsContactChanged(UserData *user, bool fromThisApp) {
	if (MainWidget *m = App::main()) m->notify_userIsContactChanged(user, fromThisApp);
}

void botCommandsChanged(UserData *user) {
	if (MainWidget *m = App::main()) {
		m->notify_botCommandsChanged(user);
	}
	peerUpdatedDelayed(user, PeerUpdate::Flag::BotCommandsChanged);
}

void inlineBotRequesting(bool requesting) {
	if (MainWidget *m = App::main()) m->notify_inlineBotRequesting(requesting);
}

void replyMarkupUpdated(const HistoryItem *item) {
	if (MainWidget *m = App::main()) {
		m->notify_replyMarkupUpdated(item);
	}
}

void inlineKeyboardMoved(const HistoryItem *item, int oldKeyboardTop, int newKeyboardTop) {
	if (MainWidget *m = App::main()) {
		m->notify_inlineKeyboardMoved(item, oldKeyboardTop, newKeyboardTop);
	}
}

bool switchInlineBotButtonReceived(const QString &query, UserData *samePeerBot, MsgId samePeerReplyTo) {
	if (auto main = App::main()) {
		return main->notify_switchInlineBotButtonReceived(query, samePeerBot, samePeerReplyTo);
	}
	return false;
}

void migrateUpdated(PeerData *peer) {
	if (MainWidget *m = App::main()) m->notify_migrateUpdated(peer);
}

void historyItemLayoutChanged(const HistoryItem *item) {
	if (MainWidget *m = App::main()) m->notify_historyItemLayoutChanged(item);
}

void historyMuteUpdated(History *history) {
	if (MainWidget *m = App::main()) m->notify_historyMuteUpdated(history);
}

void handlePendingHistoryUpdate() {
	if (auto main = App::main()) {
		main->notify_handlePendingHistoryUpdate();
	}
	for (auto item : base::take(Global::RefPendingRepaintItems())) {
		Ui::repaintHistoryItem(item);

		// Start the video if it is waiting for that.
		if (item->pendingInitDimensions()) {
			if (auto media = item->getMedia()) {
				if (auto reader = media->getClipReader()) {
					if (!reader->started() && reader->mode() == Media::Clip::Reader::Mode::Video) {
						item->history()->resizeGetHeight(item->history()->width);
					}
				}
			}
		}
	}
}

void unreadCounterUpdated() {
	Global::RefHandleUnreadCounterUpdate().call();
}

} // namespace Notify

#define DefineReadOnlyVar(Namespace, Type, Name) const Type &Name() { \
	t_assert_full(Namespace##Data != 0, #Namespace "Data != nullptr in " #Namespace "::" #Name, __FILE__, __LINE__); \
	return Namespace##Data->Name; \
}
#define DefineRefVar(Namespace, Type, Name) DefineReadOnlyVar(Namespace, Type, Name) \
Type &Ref##Name() { \
	t_assert_full(Namespace##Data != 0, #Namespace "Data != nullptr in " #Namespace "::Ref" #Name, __FILE__, __LINE__); \
	return Namespace##Data->Name; \
}
#define DefineVar(Namespace, Type, Name) DefineRefVar(Namespace, Type, Name) \
void Set##Name(const Type &Name) { \
	t_assert_full(Namespace##Data != 0, #Namespace "Data != nullptr in " #Namespace "::Set" #Name, __FILE__, __LINE__); \
	Namespace##Data->Name = Name; \
}

namespace Sandbox {
namespace internal {

struct Data {
	QString LangSystemISO;
	int32 LangSystem = languageDefault;

	QByteArray LastCrashDump;
	ProxyData PreLaunchProxy;
};

} // namespace internal
} // namespace Sandbox

std::unique_ptr<Sandbox::internal::Data> SandboxData;
uint64 SandboxUserTag = 0;

namespace Sandbox {

bool CheckBetaVersionDir() {
	QFile beta(cExeDir() + qsl("TelegramBeta_data/tdata/beta"));
	if (cBetaVersion()) {
		cForceWorkingDir(cExeDir() + qsl("TelegramBeta_data/"));
		QDir().mkpath(cWorkingDir() + qstr("tdata"));
		if (*BetaPrivateKey) {
			cSetBetaPrivateKey(QByteArray(BetaPrivateKey));
		}
		if (beta.open(QIODevice::WriteOnly)) {
			QDataStream dataStream(&beta);
			dataStream.setVersion(QDataStream::Qt_5_3);
			dataStream << quint64(cRealBetaVersion()) << cBetaPrivateKey();
		} else {
			LOG(("FATAL: Could not open '%1' for writing private key!").arg(beta.fileName()));
			return false;
		}
	} else if (beta.exists()) {
		cForceWorkingDir(cExeDir() + qsl("TelegramBeta_data/"));
		if (beta.open(QIODevice::ReadOnly)) {
			QDataStream dataStream(&beta);
			dataStream.setVersion(QDataStream::Qt_5_3);

			quint64 v;
			QByteArray k;
			dataStream >> v >> k;
			if (dataStream.status() == QDataStream::Ok) {
				cSetBetaVersion(qMax(v, AppVersion * 1000ULL));
				cSetBetaPrivateKey(k);
				cSetRealBetaVersion(v);
			} else {
				LOG(("FATAL: '%1' is corrupted, reinstall private beta!").arg(beta.fileName()));
				return false;
			}
		} else {
			LOG(("FATAL: could not open '%1' for reading private key!").arg(beta.fileName()));
			return false;
		}
	}
	return true;
}

void WorkingDirReady() {
	if (QFile(cWorkingDir() + qsl("tdata/withtestmode")).exists()) {
		cSetTestMode(true);
	}
	if (!cDebug() && QFile(cWorkingDir() + qsl("tdata/withdebug")).exists()) {
		cSetDebug(true);
	}
	if (cBetaVersion()) {
		cSetAlphaVersion(false);
	} else if (!cAlphaVersion() && QFile(cWorkingDir() + qsl("tdata/devversion")).exists()) {
		cSetAlphaVersion(true);
	} else if (AppAlphaVersion) {
		QFile f(cWorkingDir() + qsl("tdata/devversion"));
		if (!f.exists() && f.open(QIODevice::WriteOnly)) {
			f.write("1");
		}
	}

	srand((int32)time(NULL));

	SandboxUserTag = 0;
	QFile usertag(cWorkingDir() + qsl("tdata/usertag"));
	if (usertag.open(QIODevice::ReadOnly)) {
		if (usertag.read(reinterpret_cast<char*>(&SandboxUserTag), sizeof(uint64)) != sizeof(uint64)) {
			SandboxUserTag = 0;
		}
		usertag.close();
	}
	if (!SandboxUserTag) {
		do {
			memsetrnd_bad(SandboxUserTag);
		} while (!SandboxUserTag);

		if (usertag.open(QIODevice::WriteOnly)) {
			usertag.write(reinterpret_cast<char*>(&SandboxUserTag), sizeof(uint64));
			usertag.close();
		}
	}
}

object_ptr<SingleQueuedInvokation> MainThreadTaskHandler = { nullptr };

void MainThreadTaskAdded() {
	if (!started()) {
		return;
	}

	MainThreadTaskHandler->call();
}

void start() {
	MainThreadTaskHandler.create([] {
		base::TaskQueue::ProcessMainTasks();
	});
	SandboxData = std::make_unique<internal::Data>();

	SandboxData->LangSystemISO = psCurrentLanguage();
	if (SandboxData->LangSystemISO.isEmpty()) SandboxData->LangSystemISO = qstr("en");
	auto l = LangSystemISO().toLatin1();
	for (auto i = 0; i < languageCount; ++i) {
		if (l.at(0) == LanguageCodes[i][0] && l.at(1) == LanguageCodes[i][1]) {
			SandboxData->LangSystem = i;
			break;
		}
	}
}

bool started() {
	return (SandboxData != nullptr);
}

void finish() {
	SandboxData.reset();
	MainThreadTaskHandler.destroy();
}

uint64 UserTag() {
	return SandboxUserTag;
}

DefineReadOnlyVar(Sandbox, QString, LangSystemISO);
DefineReadOnlyVar(Sandbox, int32, LangSystem);
DefineVar(Sandbox, QByteArray, LastCrashDump);
DefineVar(Sandbox, ProxyData, PreLaunchProxy);

} // namespace Sandbox

namespace Stickers {

Set *feedSet(const MTPDstickerSet &set) {
	MTPDstickerSet::Flags flags = 0;

	auto &sets = Global::RefStickerSets();
	auto it = sets.find(set.vid.v);
	auto title = stickerSetTitle(set);
	if (it == sets.cend()) {
		it = sets.insert(set.vid.v, Stickers::Set(set.vid.v, set.vaccess_hash.v, title, qs(set.vshort_name), set.vcount.v, set.vhash.v, set.vflags.v | MTPDstickerSet_ClientFlag::f_not_loaded));
	} else {
		it->access = set.vaccess_hash.v;
		it->title = title;
		it->shortName = qs(set.vshort_name);
		flags = it->flags;
		auto clientFlags = it->flags & (MTPDstickerSet_ClientFlag::f_featured | MTPDstickerSet_ClientFlag::f_unread | MTPDstickerSet_ClientFlag::f_not_loaded | MTPDstickerSet_ClientFlag::f_special);
		it->flags = set.vflags.v | clientFlags;
		if (it->count != set.vcount.v || it->hash != set.vhash.v || it->emoji.isEmpty()) {
			it->count = set.vcount.v;
			it->hash = set.vhash.v;
			it->flags |= MTPDstickerSet_ClientFlag::f_not_loaded; // need to request this set
		}
	}
	auto changedFlags = (flags ^ it->flags);
	if (changedFlags & MTPDstickerSet::Flag::f_archived) {
		auto index = Global::ArchivedStickerSetsOrder().indexOf(it->id);
		if (it->flags & MTPDstickerSet::Flag::f_archived) {
			if (index < 0) {
				Global::RefArchivedStickerSetsOrder().push_front(it->id);
			}
		} else if (index >= 0) {
			Global::RefArchivedStickerSetsOrder().removeAt(index);
		}
	}
	return &it.value();
}

} // namespace Stickers

namespace Global {
namespace internal {

struct Data {
	SingleQueuedInvokation HandleHistoryUpdate = { [] { App::app()->call_handleHistoryUpdate(); } };
	SingleQueuedInvokation HandleUnreadCounterUpdate = { [] { App::app()->call_handleUnreadCounterUpdate(); } };
	SingleQueuedInvokation HandleDelayedPeerUpdates = { [] { App::app()->call_handleDelayedPeerUpdates(); } };
	SingleQueuedInvokation HandleObservables = { [] { App::app()->call_handleObservables(); } };

	Adaptive::WindowLayout AdaptiveWindowLayout = Adaptive::WindowLayout::Normal;
	Adaptive::ChatLayout AdaptiveChatLayout = Adaptive::ChatLayout::Normal;
	bool AdaptiveForWide = true;
	base::Observable<void> AdaptiveChanged;

	bool DialogsModeEnabled = false;
	Dialogs::Mode DialogsMode = Dialogs::Mode::All;
	bool ModerateModeEnabled = false;

	bool ScreenIsLocked = false;

	int32 DebugLoggingFlags = 0;

	float64 RememberedSongVolume = kDefaultVolume;
	float64 SongVolume = kDefaultVolume;
	base::Observable<void> SongVolumeChanged;
	float64 VideoVolume = kDefaultVolume;
	base::Observable<void> VideoVolumeChanged;

	// config
	int32 ChatSizeMax = 200;
	int32 MegagroupSizeMax = 1000;
	int32 ForwardedCountMax = 100;
	int32 OnlineUpdatePeriod = 120000;
	int32 OfflineBlurTimeout = 5000;
	int32 OfflineIdleTimeout = 30000;
	int32 OnlineFocusTimeout = 1000;
	int32 OnlineCloudTimeout = 300000;
	int32 NotifyCloudDelay = 30000;
	int32 NotifyDefaultDelay = 1500;
	int32 ChatBigSize = 10;
	int32 PushChatPeriod = 60000;
	int32 PushChatLimit = 2;
	int32 SavedGifsLimit = 200;
	int32 EditTimeLimit = 172800;
	int32 StickersRecentLimit = 30;
	int32 PinnedDialogsCountMax = 5;
	QString InternalLinksDomain = qsl("https://t.me/");
	int32 CallReceiveTimeoutMs = 20000;
	int32 CallRingTimeoutMs = 90000;
	int32 CallConnectTimeoutMs = 30000;
	int32 CallPacketTimeoutMs = 10000;
	bool PhoneCallsEnabled = true;
	base::Observable<void> PhoneCallsEnabledChanged;

	HiddenPinnedMessagesMap HiddenPinnedMessages;

	PendingItemsMap PendingRepaintItems;

	Stickers::Sets StickerSets;
	Stickers::Order StickerSetsOrder;
	TimeMs LastStickersUpdate = 0;
	TimeMs LastRecentStickersUpdate = 0;
	Stickers::Order FeaturedStickerSetsOrder;
	int FeaturedStickerSetsUnreadCount = 0;
	base::Observable<void> FeaturedStickerSetsUnreadCountChanged;
	TimeMs LastFeaturedStickersUpdate = 0;
	Stickers::Order ArchivedStickerSetsOrder;

	CircleMasksMap CircleMasks;

	base::Observable<void> SelfChanged;

	bool AskDownloadPath = false;
	QString DownloadPath;
	QByteArray DownloadPathBookmark;
	base::Observable<void> DownloadPathChanged;

	bool SoundNotify = true;
	bool DesktopNotify = true;
	bool RestoreSoundNotifyFromTray = false;
	bool IncludeMuted = true;
	DBINotifyView NotifyView = dbinvShowPreview;
	bool NativeNotifications = false;
	int NotificationsCount = 3;
	Notify::ScreenCorner NotificationsCorner = Notify::ScreenCorner::BottomRight;
	bool NotificationsDemoIsShown = false;

	DBIConnectionType ConnectionType = dbictAuto;
	bool TryIPv6 = (cPlatform() == dbipWindows) ? false : true;
	ProxyData ConnectionProxy;
	base::Observable<void> ConnectionTypeChanged;

	base::Observable<void> ChooseCustomLang;

	int AutoLock = 3600;
	bool LocalPasscode = false;
	base::Observable<void> LocalPasscodeChanged;

	base::Variable<DBIWorkMode> WorkMode = { dbiwmWindowAndTray };

	base::Observable<HistoryItem*> ItemRemoved;
	base::Observable<void> UnreadCounterUpdate;
	base::Observable<void> PeerChooseCancel;

};

} // namespace internal
} // namespace Global

Global::internal::Data *GlobalData = nullptr;

namespace Global {

bool started() {
	return GlobalData != nullptr;
}

void start() {
	GlobalData = new internal::Data();
}

void finish() {
	delete GlobalData;
	GlobalData = nullptr;
}

DefineRefVar(Global, SingleQueuedInvokation, HandleHistoryUpdate);
DefineRefVar(Global, SingleQueuedInvokation, HandleUnreadCounterUpdate);
DefineRefVar(Global, SingleQueuedInvokation, HandleDelayedPeerUpdates);
DefineRefVar(Global, SingleQueuedInvokation, HandleObservables);

DefineVar(Global, Adaptive::WindowLayout, AdaptiveWindowLayout);
DefineVar(Global, Adaptive::ChatLayout, AdaptiveChatLayout);
DefineVar(Global, bool, AdaptiveForWide);
DefineRefVar(Global, base::Observable<void>, AdaptiveChanged);

DefineVar(Global, bool, DialogsModeEnabled);
DefineVar(Global, Dialogs::Mode, DialogsMode);
DefineVar(Global, bool, ModerateModeEnabled);

DefineVar(Global, bool, ScreenIsLocked);

DefineVar(Global, int32, DebugLoggingFlags);

DefineVar(Global, float64, RememberedSongVolume);
DefineVar(Global, float64, SongVolume);
DefineRefVar(Global, base::Observable<void>, SongVolumeChanged);
DefineVar(Global, float64, VideoVolume);
DefineRefVar(Global, base::Observable<void>, VideoVolumeChanged);

// config
DefineVar(Global, int32, ChatSizeMax);
DefineVar(Global, int32, MegagroupSizeMax);
DefineVar(Global, int32, ForwardedCountMax);
DefineVar(Global, int32, OnlineUpdatePeriod);
DefineVar(Global, int32, OfflineBlurTimeout);
DefineVar(Global, int32, OfflineIdleTimeout);
DefineVar(Global, int32, OnlineFocusTimeout);
DefineVar(Global, int32, OnlineCloudTimeout);
DefineVar(Global, int32, NotifyCloudDelay);
DefineVar(Global, int32, NotifyDefaultDelay);
DefineVar(Global, int32, ChatBigSize);
DefineVar(Global, int32, PushChatPeriod);
DefineVar(Global, int32, PushChatLimit);
DefineVar(Global, int32, SavedGifsLimit);
DefineVar(Global, int32, EditTimeLimit);
DefineVar(Global, int32, StickersRecentLimit);
DefineVar(Global, int32, PinnedDialogsCountMax);
DefineVar(Global, QString, InternalLinksDomain);
DefineVar(Global, int32, CallReceiveTimeoutMs);
DefineVar(Global, int32, CallRingTimeoutMs);
DefineVar(Global, int32, CallConnectTimeoutMs);
DefineVar(Global, int32, CallPacketTimeoutMs);
DefineVar(Global, bool, PhoneCallsEnabled);
DefineRefVar(Global, base::Observable<void>, PhoneCallsEnabledChanged);

DefineVar(Global, HiddenPinnedMessagesMap, HiddenPinnedMessages);

DefineRefVar(Global, PendingItemsMap, PendingRepaintItems);

DefineVar(Global, Stickers::Sets, StickerSets);
DefineVar(Global, Stickers::Order, StickerSetsOrder);
DefineVar(Global, TimeMs, LastStickersUpdate);
DefineVar(Global, TimeMs, LastRecentStickersUpdate);
DefineVar(Global, Stickers::Order, FeaturedStickerSetsOrder);
DefineVar(Global, int, FeaturedStickerSetsUnreadCount);
DefineRefVar(Global, base::Observable<void>, FeaturedStickerSetsUnreadCountChanged);
DefineVar(Global, TimeMs, LastFeaturedStickersUpdate);
DefineVar(Global, Stickers::Order, ArchivedStickerSetsOrder);

DefineRefVar(Global, CircleMasksMap, CircleMasks);

DefineRefVar(Global, base::Observable<void>, SelfChanged);

DefineVar(Global, bool, AskDownloadPath);
DefineVar(Global, QString, DownloadPath);
DefineVar(Global, QByteArray, DownloadPathBookmark);
DefineRefVar(Global, base::Observable<void>, DownloadPathChanged);

DefineVar(Global, bool, SoundNotify);
DefineVar(Global, bool, DesktopNotify);
DefineVar(Global, bool, RestoreSoundNotifyFromTray);
DefineVar(Global, bool, IncludeMuted);
DefineVar(Global, DBINotifyView, NotifyView);
DefineVar(Global, bool, NativeNotifications);
DefineVar(Global, int, NotificationsCount);
DefineVar(Global, Notify::ScreenCorner, NotificationsCorner);
DefineVar(Global, bool, NotificationsDemoIsShown);

DefineVar(Global, DBIConnectionType, ConnectionType);
DefineVar(Global, bool, TryIPv6);
DefineVar(Global, ProxyData, ConnectionProxy);
DefineRefVar(Global, base::Observable<void>, ConnectionTypeChanged);

DefineRefVar(Global, base::Observable<void>, ChooseCustomLang);

DefineVar(Global, int, AutoLock);
DefineVar(Global, bool, LocalPasscode);
DefineRefVar(Global, base::Observable<void>, LocalPasscodeChanged);

DefineRefVar(Global, base::Variable<DBIWorkMode>, WorkMode);

DefineRefVar(Global, base::Observable<HistoryItem*>, ItemRemoved);
DefineRefVar(Global, base::Observable<void>, UnreadCounterUpdate);
DefineRefVar(Global, base::Observable<void>, PeerChooseCancel);

} // namespace Global
