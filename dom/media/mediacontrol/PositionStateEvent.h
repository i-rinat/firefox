/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* THIS FILE IS AUTOGENERATED FROM PositionStateEvent.webidl BY Codegen.py - DO
 * NOT EDIT */

#ifndef mozilla_dom_PositionStateEvent_h
#define mozilla_dom_PositionStateEvent_h

#include "mozilla/Attributes.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/PositionStateEventBinding.h"

struct JSContext;
namespace mozilla {
namespace dom {

class PositionStateEvent : public Event {
 public:
  NS_INLINE_DECL_REFCOUNTING_INHERITED(PositionStateEvent, Event)

 protected:
  virtual ~PositionStateEvent();
  explicit PositionStateEvent(mozilla::dom::EventTarget* aOwner);

  double mDuration;
  double mPlaybackRate;
  double mPosition;

 public:
  PositionStateEvent* AsPositionStateEvent() override;

  JSObject* WrapObjectInternal(JSContext* aCx,
                               JS::Handle<JSObject*> aGivenProto) override;

  static already_AddRefed<PositionStateEvent> Constructor(
      mozilla::dom::EventTarget* aOwner, const nsAString& aType,
      const PositionStateEventInit& aEventInitDict);

  static already_AddRefed<PositionStateEvent> Constructor(
      const GlobalObject& aGlobal, const nsAString& aType,
      const PositionStateEventInit& aEventInitDict);

  double Duration() const;

  double PlaybackRate() const;

  double Position() const;
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_PositionStateEvent_h
