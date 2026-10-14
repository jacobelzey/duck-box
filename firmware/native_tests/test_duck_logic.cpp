// Native unit tests for the counting state machine (src/duck_logic.*).
//
// The tests use the default thresholds written in LogicConfig (5 ms drop
// debounce, 400 ms drop lockout, 4 s full, 2 s full-clear, 60 ms shelf
// minimum, 1.5 s shelf lockout, 15 s parked, 60 s trade window), so they do
// not change when include/config.h is tuned.
#include <algorithm>
#include <string>
#include <vector>

#include "config.h"
#include "duck_logic.h"
#include "logic_config.h"
#include "mini_test.h"

using duck::Beam;
using duck::DuckLogic;
using duck::Event;
using duck::EventType;
using duck::LogicConfig;

namespace {

// Drives a DuckLogic the way the firmware does: a tick every couple of
// milliseconds, edges in between, events drained after every call.
struct Sim {
  DuckLogic logic;
  uint32_t now;
  std::vector<Event> events;

  explicit Sim(uint32_t start = 1000, const LogicConfig& cfg = LogicConfig())
      : logic(cfg), now(start) {
    logic.begin(now);
  }

  void drain() {
    Event e;
    while (logic.popEvent(e)) events.push_back(e);
  }

  void run(uint32_t ms, uint32_t step = 2) {
    uint32_t left = ms;
    while (left > 0) {
      const uint32_t d = std::min(step, left);
      now += d;
      left -= d;
      logic.tick(now);
      drain();
    }
  }

  void edge(Beam beam, bool blocked) {
    logic.onBeamEdge(beam, blocked, now);
    drain();
  }

  // Block a beam for `ms`, then clear it.
  void dropPulse(uint32_t ms) {
    edge(Beam::Drop, true);
    run(ms);
    edge(Beam::Drop, false);
  }
  void shelfPulse(uint32_t ms) {
    edge(Beam::Shelf, true);
    run(ms);
    edge(Beam::Shelf, false);
  }

  int count(EventType type) const {
    int n = 0;
    for (const auto& e : events) n += (e.type == type);
    return n;
  }

  // All events so far, e.g. "DROP,REACH_IN,TRADE".
  std::string seq() const {
    std::string s;
    for (const auto& e : events) {
      if (!s.empty()) s += ",";
      s += duck::eventName(e.type);
    }
    return s;
  }

  uint32_t timeOf(EventType type, int nth = 0) const {
    for (const auto& e : events) {
      if (e.type == type && nth-- == 0) return e.timeMs;
    }
    return 0xDEADBEEF;
  }
};

}  // namespace

// ------------------------------------------------------------------ drops

TEST(single_drop_counts_once) {
  Sim s;
  s.run(500);
  s.dropPulse(40);
  s.run(3000);
  CHECK_EQ(s.seq(), std::string("DROP"));
  CHECK_EQ(s.logic.counters().drops, 1u);
  CHECK_EQ(s.logic.counters().reachIns, 0u);
  CHECK_EQ(s.logic.counters().trades, 0u);
}

TEST(drop_is_counted_on_the_break_after_debounce) {
  Sim s;                 // starts at t = 1000
  s.run(500);            // t = 1500
  s.edge(Beam::Drop, true);
  s.run(4);              // 4 ms blocked: not yet believed
  CHECK_EQ(s.count(EventType::Drop), 0);
  s.run(2);              // 6 ms blocked: counted while still blocked
  CHECK_EQ(s.count(EventType::Drop), 1);
  CHECK_EQ(s.timeOf(EventType::Drop), 1505u);
  s.run(30);
  s.edge(Beam::Drop, false);
  s.run(1000);
  CHECK_EQ(s.count(EventType::Drop), 1);
}

TEST(drop_glitch_shorter_than_debounce_is_ignored) {
  Sim s;
  s.run(100);
  s.dropPulse(2);
  s.run(1000);
  CHECK_EQ(s.seq(), std::string(""));
  // ...and it must not start a lockout that hides a real duck.
  s.dropPulse(2);
  s.run(50);
  s.dropPulse(40);
  s.run(1000);
  CHECK_EQ(s.logic.counters().drops, 1u);
}

TEST(tumbling_duck_is_one_count) {
  Sim s;
  s.run(100);
  s.dropPulse(30);   // first break
  s.run(50);
  s.dropPulse(20);   // bounces back through the beam
  s.run(100);
  s.dropPulse(30);   // and again
  s.run(300);
  s.dropPulse(15);   // still within 400 ms of the previous break ending
  s.run(2000);
  CHECK_EQ(s.logic.counters().drops, 1u);
  CHECK_EQ(s.seq(), std::string("DROP"));
}

TEST(two_separate_drops_are_two_counts) {
  Sim s;
  s.run(100);
  s.dropPulse(40);
  s.run(1000);
  s.dropPulse(40);
  s.run(1000);
  CHECK_EQ(s.logic.counters().drops, 2u);
}

TEST(drop_lockout_boundary) {
  // Lockout is measured from the end of the previous break.
  {
    Sim s;
    s.dropPulse(40);
    s.run(398);
    s.dropPulse(40);
    s.run(1000);
    CHECK_EQ(s.logic.counters().drops, 1u);
  }
  {
    Sim s;
    s.dropPulse(40);
    s.run(400);
    s.dropPulse(40);
    s.run(1000);
    CHECK_EQ(s.logic.counters().drops, 2u);
  }
}

TEST(lockout_decision_uses_the_start_of_the_break) {
  // A break that begins 398 ms after the previous one ended is inside the
  // lockout even though its debounce expires after the 400 ms mark.
  Sim s;
  s.dropPulse(40);
  s.run(398, 1);
  s.edge(Beam::Drop, true);
  s.run(20, 1);
  s.edge(Beam::Drop, false);
  s.run(1000);
  CHECK_EQ(s.logic.counters().drops, 1u);
}

// ------------------------------------------------------------------- full

TEST(full_and_recovery) {
  Sim s;
  s.run(100);                    // t = 1100
  s.edge(Beam::Drop, true);      // duck lands and stays in the beam
  s.run(3990);
  CHECK_EQ(s.seq(), std::string("DROP"));       // counted once, not yet full
  CHECK(!s.logic.isFull());
  s.run(20);
  CHECK_EQ(s.seq(), std::string("DROP,FULL_ON"));
  CHECK(s.logic.isFull());
  CHECK_EQ(s.timeOf(EventType::FullOn), 1100u + 4000u);

  s.run(60000);                  // stays full for a minute: nothing more
  CHECK_EQ(s.seq(), std::string("DROP,FULL_ON"));

  s.edge(Beam::Drop, false);     // somebody takes ducks out, pile settles
  s.run(1000);
  s.dropPulse(300);              // pile shifts through the beam: not a drop
  s.run(1900);
  CHECK(s.logic.isFull());       // clear timer restarted by the re-block
  s.run(200);
  CHECK(!s.logic.isFull());
  CHECK_EQ(s.seq(), std::string("DROP,FULL_ON,FULL_OFF"));
  CHECK_EQ(s.logic.counters().drops, 1u);

  s.dropPulse(40);               // normal counting resumes
  s.run(1000);
  CHECK_EQ(s.logic.counters().drops, 2u);
  CHECK_EQ(s.seq(), std::string("DROP,FULL_ON,FULL_OFF,DROP"));
}

TEST(nothing_is_counted_while_full) {
  Sim s;
  s.edge(Beam::Drop, true);
  s.run(5000);
  CHECK(s.logic.isFull());
  // Beam flickers as people poke the pile; never clear for 2 s.
  for (int i = 0; i < 10; ++i) {
    s.edge(Beam::Drop, false);
    s.run(500);
    s.edge(Beam::Drop, true);
    s.run(500);
  }
  CHECK(s.logic.isFull());
  CHECK_EQ(s.logic.counters().drops, 1u);
  CHECK_EQ(s.count(EventType::FullOn), 1);
  CHECK_EQ(s.count(EventType::FullOff), 0);
}

TEST(block_just_under_full_time_is_not_full) {
  Sim s;
  s.dropPulse(3990);
  s.run(5000);
  CHECK_EQ(s.seq(), std::string("DROP"));
  CHECK(!s.logic.isFull());
}

// ------------------------------------------------------------------ shelf

TEST(shelf_glitch_is_ignored) {
  Sim s;
  s.run(100);
  s.shelfPulse(30);
  s.run(3000);
  CHECK_EQ(s.seq(), std::string(""));
  CHECK_EQ(s.logic.counters().reachIns, 0u);
  // A glitch must not start a lockout either.
  s.shelfPulse(30);
  s.run(100);
  s.shelfPulse(500);
  s.run(3000);
  CHECK_EQ(s.logic.counters().reachIns, 1u);
}

TEST(normal_reach_in_counts_when_the_beam_clears) {
  Sim s;                    // t = 1000
  s.run(100);               // t = 1100
  s.edge(Beam::Shelf, true);
  s.run(800);
  CHECK_EQ(s.count(EventType::ReachIn), 0);   // hand still inside
  s.edge(Beam::Shelf, false);
  CHECK_EQ(s.count(EventType::ReachIn), 1);
  CHECK_EQ(s.timeOf(EventType::ReachIn), 1900u);
  s.run(5000);
  CHECK_EQ(s.logic.counters().reachIns, 1u);
  CHECK_EQ(s.seq(), std::string("REACH_IN"));
}

TEST(shelf_minimum_block_boundary) {
  {
    Sim s;
    s.shelfPulse(58);
    s.run(3000);
    CHECK_EQ(s.logic.counters().reachIns, 0u);
  }
  {
    Sim s;
    s.shelfPulse(60);
    s.run(3000);
    CHECK_EQ(s.logic.counters().reachIns, 1u);
  }
}

TEST(one_hand_visit_with_fumbling_is_one_count) {
  Sim s;
  s.shelfPulse(600);   // counted when it clears
  s.run(500);
  s.shelfPulse(700);   // back in within 1.5 s: same visit
  s.run(1000);
  s.shelfPulse(400);   // and again, within 1.5 s of the last exit
  s.run(5000);
  CHECK_EQ(s.logic.counters().reachIns, 1u);
}

TEST(two_hand_visits_are_two_counts) {
  Sim s;
  s.shelfPulse(600);
  s.run(1600);
  s.shelfPulse(600);
  s.run(5000);
  CHECK_EQ(s.logic.counters().reachIns, 2u);
}

TEST(long_parked_duck) {
  Sim s;                         // t = 1000
  s.edge(Beam::Shelf, true);     // a duck slides into the beam and stays
  s.run(14990);
  CHECK_EQ(s.seq(), std::string(""));
  s.run(20);
  CHECK_EQ(s.seq(), std::string("SHELF_STUCK_ON"));
  CHECK_EQ(s.timeOf(EventType::ShelfStuckOn), 16000u);
  CHECK(s.logic.isShelfStuck());

  s.run(120000);                 // two minutes later it is still there
  CHECK_EQ(s.seq(), std::string("SHELF_STUCK_ON"));

  s.edge(Beam::Shelf, false);    // somebody pushes it back
  CHECK_EQ(s.count(EventType::ReachIn), 0);   // not a reach-in
  s.run(1000);
  s.shelfPulse(200);             // their hand passes the beam again
  s.run(1900);
  CHECK(s.logic.isShelfStuck()); // clear timer restarted
  s.run(200);
  CHECK(!s.logic.isShelfStuck());
  CHECK_EQ(s.seq(), std::string("SHELF_STUCK_ON,SHELF_STUCK_OFF"));
  CHECK_EQ(s.logic.counters().reachIns, 0u);

  s.shelfPulse(500);             // normal counting resumes
  s.run(3000);
  CHECK_EQ(s.logic.counters().reachIns, 1u);
}

TEST(long_but_not_parked_reach_in_still_counts) {
  Sim s;
  s.shelfPulse(14000);   // someone choosing a duck very carefully
  s.run(3000);
  CHECK_EQ(s.seq(), std::string("REACH_IN"));
}

// ----------------------------------------------------------------- trades

TEST(trade_drop_then_reach_in) {
  Sim s;
  s.dropPulse(40);
  s.run(10000);
  s.shelfPulse(800);
  s.run(3000);
  CHECK_EQ(s.seq(), std::string("DROP,REACH_IN,TRADE"));
  CHECK_EQ(s.logic.counters().drops, 1u);
  CHECK_EQ(s.logic.counters().reachIns, 1u);
  CHECK_EQ(s.logic.counters().trades, 1u);
}

TEST(trade_reach_in_then_drop) {
  Sim s;
  s.shelfPulse(800);
  s.run(10000);
  s.dropPulse(40);
  s.run(3000);
  CHECK_EQ(s.seq(), std::string("REACH_IN,DROP,TRADE"));
  CHECK_EQ(s.logic.counters().trades, 1u);
}

TEST(trade_pairing_is_used_only_once) {
  Sim s;
  s.dropPulse(40);
  s.run(2000);
  s.shelfPulse(800);       // pairs with the drop
  s.run(5000);
  s.shelfPulse(800);       // the drop is already used: no second trade
  s.run(5000);
  CHECK_EQ(s.logic.counters().trades, 1u);
  CHECK_EQ(s.seq(), std::string("DROP,REACH_IN,TRADE,REACH_IN"));

  s.dropPulse(40);         // this drop pairs with the second reach-in
  s.run(5000);
  CHECK_EQ(s.logic.counters().trades, 2u);
  s.dropPulse(40);         // nothing left to pair with
  s.run(5000);
  CHECK_EQ(s.logic.counters().trades, 2u);
  CHECK_EQ(s.logic.counters().drops, 3u);
  CHECK_EQ(s.logic.counters().reachIns, 2u);
}

TEST(several_drops_pair_one_by_one) {
  Sim s;
  for (int i = 0; i < 3; ++i) {
    s.dropPulse(40);
    s.run(1000);
  }
  for (int i = 0; i < 4; ++i) {
    s.shelfPulse(500);
    s.run(2000);
  }
  CHECK_EQ(s.logic.counters().drops, 3u);
  CHECK_EQ(s.logic.counters().reachIns, 4u);
  CHECK_EQ(s.logic.counters().trades, 3u);
}

TEST(trade_window_expires) {
  Sim s;
  s.dropPulse(40);
  s.run(61000);
  s.shelfPulse(800);
  s.run(3000);
  CHECK_EQ(s.seq(), std::string("DROP,REACH_IN"));
  CHECK_EQ(s.logic.counters().trades, 0u);
  // The unpaired reach-in is now the one waiting; a drop soon after pairs.
  s.dropPulse(40);
  s.run(1000);
  CHECK_EQ(s.logic.counters().trades, 1u);
}

TEST(trade_window_boundary) {
  // Drop counted at 1005; reach-in counted exactly 60 000 ms later pairs,
  // one millisecond later does not.
  {
    Sim s;
    s.edge(Beam::Drop, true);
    s.run(40, 1);
    s.edge(Beam::Drop, false);
    s.run(1005 + 60000 - s.now - 100, 1);
    s.edge(Beam::Shelf, true);
    s.run(100, 1);
    s.edge(Beam::Shelf, false);
    CHECK_EQ(s.timeOf(EventType::ReachIn) - s.timeOf(EventType::Drop), 60000u);
    CHECK_EQ(s.logic.counters().trades, 1u);
  }
  {
    Sim s;
    s.edge(Beam::Drop, true);
    s.run(40, 1);
    s.edge(Beam::Drop, false);
    s.run(1005 + 60001 - s.now - 100, 1);
    s.edge(Beam::Shelf, true);
    s.run(100, 1);
    s.edge(Beam::Shelf, false);
    CHECK_EQ(s.timeOf(EventType::ReachIn) - s.timeOf(EventType::Drop), 60001u);
    CHECK_EQ(s.logic.counters().trades, 0u);
  }
}

TEST(expired_partner_is_skipped_but_fresh_one_is_used) {
  Sim s;
  s.dropPulse(40);       // will expire
  s.run(50000);
  s.dropPulse(40);       // still fresh when the hand comes
  s.run(20000);          // first drop is now 70 s old, second 20 s
  s.shelfPulse(800);
  s.run(2000);
  CHECK_EQ(s.logic.counters().trades, 1u);
  s.shelfPulse(800);     // no partner left
  s.run(2000);
  CHECK_EQ(s.logic.counters().trades, 1u);
}

TEST(beams_are_independent) {
  // A parked duck on the shelf does not stop drops being counted, and a
  // full chamber does not stop reach-ins.
  Sim s;
  s.edge(Beam::Shelf, true);
  s.run(16000);
  CHECK(s.logic.isShelfStuck());
  s.dropPulse(40);
  s.run(1000);
  CHECK_EQ(s.logic.counters().drops, 1u);

  s.edge(Beam::Shelf, false);
  s.run(3000);
  s.edge(Beam::Drop, true);
  s.run(5000);
  CHECK(s.logic.isFull());
  s.shelfPulse(500);
  s.run(100);
  CHECK_EQ(s.logic.counters().reachIns, 1u);
}

// ------------------------------------------------- timing and robustness

TEST(late_delivery_of_edges_keeps_true_event_times) {
  // The main loop was busy (screen redraw) and delivers both edges of a
  // drop afterwards, without ticks in between.
  DuckLogic logic;
  logic.begin(1000);
  logic.tick(1090);
  logic.onBeamEdge(Beam::Drop, true, 1100);
  logic.onBeamEdge(Beam::Drop, false, 1140);
  logic.tick(1200);
  Event e;
  CHECK(logic.popEvent(e));
  CHECK(e.type == EventType::Drop);
  CHECK_EQ(e.timeMs, 1105u);
  CHECK(!logic.popEvent(e));
}

TEST(late_delivery_of_a_short_reach_in_and_a_drop_in_order) {
  DuckLogic logic;
  logic.begin(0);
  logic.onBeamEdge(Beam::Shelf, true, 100);
  logic.onBeamEdge(Beam::Drop, true, 120);
  logic.onBeamEdge(Beam::Drop, false, 150);
  logic.onBeamEdge(Beam::Shelf, false, 400);
  logic.tick(500);
  Event e;
  CHECK(logic.popEvent(e));
  CHECK(e.type == EventType::Drop);
  CHECK_EQ(e.timeMs, 125u);
  CHECK(logic.popEvent(e));
  CHECK(e.type == EventType::ReachIn);
  CHECK_EQ(e.timeMs, 400u);
  CHECK(logic.popEvent(e));
  CHECK(e.type == EventType::Trade);
  CHECK(!logic.popEvent(e));
}

TEST(slightly_out_of_order_timestamp_is_treated_as_now) {
  DuckLogic logic;
  logic.begin(1000);
  logic.tick(2000);
  logic.onBeamEdge(Beam::Drop, true, 1998);   // 2 ms "in the past"
  logic.tick(2010);
  logic.onBeamEdge(Beam::Drop, false, 2040);
  logic.tick(3000);
  CHECK_EQ(logic.counters().drops, 1u);
  Event e;
  CHECK(logic.popEvent(e));
  CHECK_EQ(e.timeMs, 2005u);
}

TEST(repeated_edge_reports_are_harmless) {
  Sim s;
  s.edge(Beam::Drop, false);     // already clear
  s.edge(Beam::Drop, true);
  s.run(10);
  s.edge(Beam::Drop, true);      // reported again
  s.run(30);
  s.edge(Beam::Drop, false);
  s.edge(Beam::Drop, false);
  s.run(1000);
  CHECK_EQ(s.logic.counters().drops, 1u);
}

TEST(coarse_ticks_give_the_same_result_as_fine_ticks) {
  for (uint32_t step : {1u, 2u, 7u, 25u, 40u}) {
    Sim s;
    s.run(200, step);
    s.dropPulse(40);
    s.run(2000, step);
    s.shelfPulse(900);
    s.run(2000, step);
    s.edge(Beam::Drop, true);
    s.run(6000, step);
    CHECK_EQ(s.seq(), std::string("DROP,REACH_IN,TRADE,DROP,FULL_ON"));
  }
}

TEST(begin_with_a_blocked_beam_does_not_count_it) {
  DuckLogic logic;
  logic.begin(5000, /*dropBlocked=*/true, /*shelfBlocked=*/true);
  logic.tick(5100);
  CHECK_EQ(logic.counters().drops, 0u);
  logic.onBeamEdge(Beam::Shelf, false, 6000);   // a second later it clears
  logic.tick(9100);                             // drop beam blocked for 4.1 s
  CHECK(logic.isFull());
  CHECK_EQ(logic.counters().drops, 0u);
  CHECK_EQ(logic.counters().reachIns, 0u);
}

// --------------------------------------------------------------- rollover

TEST(rollover_single_drop_across_the_wrap) {
  Sim s(0xFFFFFFF0u);            // 16 ms before millis() wraps to 0
  s.run(10);
  s.edge(Beam::Drop, true);      // at 0xFFFFFFFA
  s.run(40);                     // wraps while blocked
  s.edge(Beam::Drop, false);
  s.run(1000);
  CHECK_EQ(s.logic.counters().drops, 1u);
  CHECK_EQ(s.timeOf(EventType::Drop), 0xFFFFFFFAu + 5u);   // = 0xFFFFFFFF
  CHECK(s.now < 2000u);          // we really did wrap
}

TEST(rollover_lockout_across_the_wrap) {
  Sim s(0xFFFFFF00u);
  s.run(200);                    // 0xFFFFFFC8
  s.dropPulse(30);               // ends just before the wrap
  s.run(100);                    // now past zero
  s.dropPulse(30);               // tumbling: still locked out
  s.run(1000);
  s.dropPulse(30);               // a new duck
  s.run(1000);
  CHECK_EQ(s.logic.counters().drops, 2u);
}

TEST(rollover_full_and_recovery_across_the_wrap) {
  Sim s(0xFFFFFFFFu - 2000u);
  s.edge(Beam::Drop, true);
  s.run(3990);
  CHECK(!s.logic.isFull());
  s.run(20);
  CHECK(s.logic.isFull());
  s.edge(Beam::Drop, false);
  s.run(2100);
  CHECK(!s.logic.isFull());
  CHECK_EQ(s.seq(), std::string("DROP,FULL_ON,FULL_OFF"));
}

TEST(rollover_reach_in_parked_duck_and_trade_across_the_wrap) {
  Sim s(0xFFFFFFFFu - 30000u);
  s.dropPulse(40);               // 30 s before the wrap
  s.run(40000);                  // 10 s after the wrap
  s.shelfPulse(700);
  s.run(2000);
  CHECK_EQ(s.seq(), std::string("DROP,REACH_IN,TRADE"));

  Sim p(0xFFFFFFFFu - 5000u);
  p.edge(Beam::Shelf, true);
  p.run(16000);
  CHECK(p.logic.isShelfStuck());
  p.edge(Beam::Shelf, false);
  p.run(2100);
  CHECK(!p.logic.isShelfStuck());
  CHECK_EQ(p.logic.counters().reachIns, 0u);
}

TEST(rollover_trade_window_still_expires_across_the_wrap) {
  Sim s(0xFFFFFFFFu - 30000u);
  s.dropPulse(40);
  s.run(61000);
  s.shelfPulse(700);
  s.run(2000);
  CHECK_EQ(s.logic.counters().trades, 0u);
}

// Let the box sit idle until the wrapping clock reads `target` again
// (up to 49.7 days later), ticking once an hour and then every millisecond.
static void idleUntilClockReads(Sim& s, uint32_t target) {
  while ((uint32_t)(target - s.now) > 3600000u) s.run(3600000u, 3600000u);
  s.run((uint32_t)(target - s.now), 1);
}

TEST(very_old_drop_does_not_lock_out_after_a_wrap) {
  Sim s(1000);
  s.dropPulse(40);               // break ends at t = 1040
  s.run(5000);
  // 49.7 days later the clock reads 1140: "100 ms after" that old break.
  idleUntilClockReads(s, 1140);
  CHECK_EQ(s.now, 1140u);
  s.dropPulse(40);
  s.run(1000);
  CHECK_EQ(s.logic.counters().drops, 2u);
  CHECK_EQ(s.logic.counters().trades, 0u);
}

TEST(very_old_reach_in_does_not_pair_or_lock_out_after_a_wrap) {
  Sim s(1000);
  s.run(1000);
  s.shelfPulse(700);             // counted at t = 2700, waits for a partner
  CHECK_EQ(s.timeOf(EventType::ReachIn), 2700u);
  s.run(5000);
  // 49.7 days later the clock reads 2800: "100 ms after" that reach-in.
  idleUntilClockReads(s, 2800);
  CHECK_EQ(s.now, 2800u);
  s.dropPulse(40);               // must not trade with the ancient reach-in
  s.run(500);
  CHECK_EQ(s.logic.counters().drops, 1u);
  CHECK_EQ(s.logic.counters().trades, 0u);
  s.shelfPulse(700);             // must count, and pairs with the new drop
  s.run(1000);
  CHECK_EQ(s.logic.counters().reachIns, 2u);
  CHECK_EQ(s.logic.counters().trades, 1u);
}

// ------------------------------------------------------- pause and reset

TEST(nothing_is_counted_while_paused) {
  Sim s;
  s.logic.setPaused(true, s.now);
  s.dropPulse(40);
  s.run(1000);
  s.shelfPulse(800);
  s.run(1000);
  s.edge(Beam::Drop, true);
  s.run(20000);                  // would be FULL if not paused
  s.edge(Beam::Drop, false);
  s.run(1000);
  CHECK_EQ(s.seq(), std::string(""));
  CHECK_EQ(s.logic.counters().drops, 0u);
  CHECK(!s.logic.isFull());

  s.logic.setPaused(false, s.now);
  s.dropPulse(40);
  s.run(1000);
  CHECK_EQ(s.seq(), std::string("DROP"));
}

TEST(resume_with_blocked_beams_does_not_count_them) {
  Sim s;
  s.logic.setPaused(true, s.now);
  s.edge(Beam::Drop, true);      // owner filled the chamber to the brim
  s.edge(Beam::Shelf, true);     // and still has a hand on the shelf
  s.run(5000);
  s.logic.setPaused(false, s.now);
  s.run(1000);
  s.edge(Beam::Shelf, false);    // hand leaves: not a reach-in
  s.run(3500);
  CHECK_EQ(s.seq(), std::string("FULL_ON"));   // full 4 s after resume, no DROP
  CHECK_EQ(s.logic.counters().drops, 0u);
  CHECK_EQ(s.logic.counters().reachIns, 0u);
}

TEST(pause_forgets_pending_trade_partners_and_states) {
  Sim s;
  s.dropPulse(40);
  s.run(1000);
  s.edge(Beam::Drop, true);
  s.run(5000);
  CHECK(s.logic.isFull());
  s.logic.setPaused(true, s.now);
  CHECK(!s.logic.isFull());
  s.edge(Beam::Drop, false);
  s.run(2000);
  s.logic.setPaused(false, s.now);
  s.shelfPulse(800);             // within 60 s of the drops, but after a restock
  s.run(1000);
  CHECK_EQ(s.logic.counters().trades, 0u);
  CHECK_EQ(s.logic.counters().reachIns, 1u);
}

TEST(reset_counters_clears_tallies_and_pairing) {
  Sim s;
  s.dropPulse(40);
  s.run(1000);
  s.dropPulse(40);
  s.run(1000);
  s.shelfPulse(800);
  s.run(2000);
  CHECK_EQ(s.logic.counters().drops, 2u);
  CHECK_EQ(s.logic.counters().trades, 1u);
  s.logic.resetCounters();
  CHECK_EQ(s.logic.counters().drops, 0u);
  CHECK_EQ(s.logic.counters().reachIns, 0u);
  CHECK_EQ(s.logic.counters().trades, 0u);
  s.shelfPulse(800);             // the second drop must not pair after a reset
  s.run(2000);
  CHECK_EQ(s.logic.counters().reachIns, 1u);
  CHECK_EQ(s.logic.counters().trades, 0u);
}

TEST(set_counters_restores_saved_tallies) {
  Sim s;
  duck::Counters saved;
  saved.drops = 41;
  saved.reachIns = 30;
  saved.trades = 12;
  s.logic.setCounters(saved);
  s.dropPulse(40);
  s.run(1000);
  CHECK_EQ(s.logic.counters().drops, 42u);
  CHECK_EQ(s.logic.counters().reachIns, 30u);
  CHECK_EQ(s.logic.counters().trades, 12u);
}

TEST(event_queue_overflow_keeps_newest_and_reports_loss) {
  DuckLogic logic;
  logic.begin(0);
  uint32_t t = 0;
  for (int i = 0; i < 20; ++i) {     // 20 drops, nobody reads events
    logic.onBeamEdge(Beam::Drop, true, t);
    logic.onBeamEdge(Beam::Drop, false, t + 40);
    t += 1000;
  }
  logic.tick(t);
  CHECK_EQ(logic.counters().drops, 20u);   // tallies are never lost
  CHECK_EQ(logic.eventsLost(), 4u);
  int n = 0;
  Event e;
  uint32_t last = 0;
  while (logic.popEvent(e)) {
    ++n;
    last = e.timeMs;
  }
  CHECK_EQ(n, 16);
  CHECK_EQ(last, 19005u);
}

// --------------------------------------------- levels noted without counting

TEST(noted_level_counts_nothing_but_can_become_full_and_parked) {
  Sim s;
  s.logic.noteLevel(Beam::Drop, true, s.now);
  s.logic.noteLevel(Beam::Shelf, true, s.now);
  s.run(1000);
  CHECK_EQ(s.seq(), std::string(""));
  CHECK(s.logic.dropBlocked());
  CHECK(s.logic.shelfBlocked());
  s.run(20000);
  CHECK_EQ(s.seq(), std::string("FULL_ON,SHELF_STUCK_ON"));
  s.logic.noteLevel(Beam::Drop, false, s.now);
  s.logic.noteLevel(Beam::Shelf, false, s.now);
  s.run(5000);
  CHECK_EQ(s.seq(), std::string("FULL_ON,SHELF_STUCK_ON,FULL_OFF,SHELF_STUCK_OFF"));
  CHECK_EQ(s.logic.counters().drops, 0u);
  CHECK_EQ(s.logic.counters().reachIns, 0u);
  s.dropPulse(40);                       // and then things count as usual
  s.run(2000);
  s.shelfPulse(500);
  s.run(2000);
  CHECK_EQ(s.logic.counters().drops, 1u);
  CHECK_EQ(s.logic.counters().reachIns, 1u);
  CHECK_EQ(s.logic.counters().trades, 1u);
}

TEST(noted_level_that_clears_soon_counts_nothing_either) {
  Sim s;
  s.logic.noteLevel(Beam::Drop, true, s.now);
  s.logic.noteLevel(Beam::Shelf, true, s.now);
  s.run(700);
  s.edge(Beam::Drop, false);
  s.edge(Beam::Shelf, false);
  s.run(3000);
  CHECK_EQ(s.seq(), std::string(""));
  CHECK_EQ(s.logic.counters().drops, 0u);
  CHECK_EQ(s.logic.counters().reachIns, 0u);
}

TEST(noted_level_keeps_a_waiting_trade_partner_and_full) {
  Sim s;
  s.dropPulse(40);                       // a duck, waiting for its trade partner
  s.run(2000);
  s.logic.noteLevel(Beam::Drop, true, s.now);    // the wire comes off for a second
  s.logic.noteLevel(Beam::Shelf, true, s.now);
  s.run(1000);
  s.edge(Beam::Drop, false);
  s.edge(Beam::Shelf, false);
  s.run(3000);
  s.shelfPulse(500);                     // the hand that takes a duck in exchange
  s.run(2000);
  CHECK_EQ(s.seq(), std::string("DROP,REACH_IN,TRADE"));

  Sim full;
  full.edge(Beam::Drop, true);           // chamber full...
  full.run(5000);
  CHECK(full.logic.isFull());
  full.logic.noteLevel(Beam::Shelf, true, full.now);   // ...and the shelf beam noted
  full.run(500);
  CHECK(full.logic.isFull());            // still full, said once
  CHECK_EQ(full.count(EventType::FullOn), 1);
}

TEST(noted_level_while_paused_is_just_a_level) {
  Sim s;
  s.logic.setPaused(true, s.now);
  s.logic.noteLevel(Beam::Drop, true, s.now);
  s.run(100);
  CHECK(s.logic.dropBlocked());
  s.logic.setPaused(false, s.now);
  s.run(1000);
  CHECK_EQ(s.logic.counters().drops, 0u);
}

// ------------------------------------------------- firmware configuration

TEST(config_h_values_are_passed_to_the_logic) {
  const LogicConfig c = makeLogicConfig();
  CHECK_EQ(c.dropDebounceMs, cfg::DROP_DEBOUNCE_MS);
  CHECK_EQ(c.dropLockoutMs, cfg::DROP_LOCKOUT_MS);
  CHECK_EQ(c.fullAfterMs, cfg::DROP_FULL_AFTER_MS);
  CHECK_EQ(c.fullClearMs, cfg::DROP_FULL_CLEAR_MS);
  CHECK_EQ(c.shelfMinBlockMs, cfg::SHELF_MIN_BLOCK_MS);
  CHECK_EQ(c.shelfLockoutMs, cfg::SHELF_LOCKOUT_MS);
  CHECK_EQ(c.shelfStuckMs, cfg::SHELF_STUCK_MS);
  CHECK_EQ(c.shelfStuckClearMs, cfg::SHELF_STUCK_CLEAR_MS);
  CHECK_EQ(c.tradeWindowMs, cfg::TRADE_WINDOW_MS);
}

TEST(pin_map_matches_the_wiring_diagram) {
  // The wiring diagram was drawn from these numbers; changing a pin in
  // config.h without changing the wiring (and this test) is a mistake.
  CHECK_EQ((int)cfg::PIN_BEAM_DROP, 32);
  CHECK_EQ((int)cfg::PIN_BEAM_SHELF, 33);
  CHECK_EQ((int)cfg::LED_COUNT, 4);
  CHECK_EQ((int)cfg::PIN_LED[0], 17);
  CHECK_EQ((int)cfg::PIN_LED[1], 22);
  CHECK_EQ((int)cfg::PIN_LED[2], 23);
  CHECK_EQ((int)cfg::PIN_LED[3], 13);
  CHECK_EQ((int)cfg::PIN_VEXT_CTRL, 21);
  CHECK_EQ((int)cfg::VEXT_ON_LEVEL, 0);
  CHECK_EQ((int)cfg::PIN_BUTTON, 0);
  CHECK_EQ((int)cfg::PIN_OLED_SDA, 4);
  CHECK_EQ((int)cfg::PIN_OLED_SCL, 15);
  CHECK_EQ((int)cfg::PIN_OLED_RST, 16);
  // On-board parts, from the board's Arduino variant file.
  CHECK_EQ((int)cfg::PIN_BOARD_LED, 25);
  CHECK_EQ((int)cfg::PIN_LORA_SCK, 5);
  CHECK_EQ((int)cfg::PIN_LORA_MISO, 19);
  CHECK_EQ((int)cfg::PIN_LORA_MOSI, 27);
  CHECK_EQ((int)cfg::PIN_LORA_SS, 18);
  CHECK_EQ((int)cfg::PIN_LORA_RST, 14);
  CHECK_EQ((int)cfg::PIN_LORA_DIO0, 26);
  CHECK_EQ((int)cfg::PIN_LORA_DIO1, 35);
  CHECK_EQ((int)cfg::PIN_LORA_DIO2, 34);
}

TEST(config_h_values_are_sane) {
  CHECK(cfg::BEAM_RESYNC_MS >= 10);                  // never faster than a redraw is slow
  CHECK(cfg::TEST_ENTRY_HOLD_MS < cfg::SPLASH_MS);
  CHECK(cfg::RUNTIME_SAVE_MS >= 60000);              // flash wear: at most once a minute
  CHECK(cfg::SAVE_QUIET_MS >= 1000);
  CHECK(cfg::BUTTON_DOUBLE_GAP_MS < cfg::BUTTON_SHORT_MAX_MS);
  CHECK(cfg::DROP_DEBOUNCE_MS >= 3);                 // above 2 ms sensor response
  CHECK(cfg::DROP_DEBOUNCE_MS < 20);                 // below a fast duck
  CHECK(cfg::DROP_LOCKOUT_MS < cfg::DROP_FULL_AFTER_MS);
  CHECK(cfg::DROP_FULL_CLEAR_MS >= cfg::DROP_LOCKOUT_MS);
  CHECK(cfg::SHELF_MIN_BLOCK_MS < cfg::SHELF_STUCK_MS);
  CHECK(cfg::SHELF_STUCK_CLEAR_MS >= cfg::SHELF_LOCKOUT_MS);
  CHECK(cfg::BUTTON_SHORT_MAX_MS < cfg::BUTTON_HOLD_RESET_MS);
  CHECK(cfg::SAVE_QUIET_MS <= cfg::SAVE_MAX_WAIT_MS);
  CHECK(cfg::LED_AVERAGE_BRIGHTNESS > 0.0f && cfg::LED_AVERAGE_BRIGHTNESS <= 1.0f);
}

TEST(firmware_configuration_runs_the_basic_story) {
  // Same scenario as the README bench test, with the real config.h values.
  Sim s(1000, makeLogicConfig());
  s.dropPulse(40);
  s.run(cfg::DROP_LOCKOUT_MS + 600);
  s.shelfPulse(cfg::SHELF_MIN_BLOCK_MS + 500);
  s.run(cfg::SHELF_LOCKOUT_MS + 500);
  CHECK_EQ(s.seq(), std::string("DROP,REACH_IN,TRADE"));
}
