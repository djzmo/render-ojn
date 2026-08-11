#include "app/Cli.hpp"
#include "core/Diagnostic.hpp"
#include "core/audio/Decoder.hpp"
#include "core/audio/MemoryFile.hpp"
#include "core/audio/OggNormalizer.hpp"
#include "core/compat/CompatibilityProfile.hpp"
#include "core/crypto/Sha256.hpp"
#include "core/format/OjnParser.hpp"
#include "core/format/PackageParser.hpp"
#include "core/io/ByteReader.hpp"
#include "core/render/Mixer.hpp"
#include "../fixtures/SyntheticFixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <limits>

TEST_CASE("SHA-256 matches standard known vectors") {
    CHECK(renderojn::crypto::sha256_hex(renderojn::crypto::sha256({})) ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    const std::vector<std::uint8_t> abc{'a', 'b', 'c'};
    CHECK(renderojn::crypto::sha256_hex(renderojn::crypto::sha256(abc)) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("the o2ma121 profile changes only its exact two-hash background event") {
    const auto& profile = renderojn::compat::known_ojn_ojm_profiles().front();
    CHECK(renderojn::crypto::sha256_hex(profile.ojn_sha256) ==
          "fc0b3d841a0f8fef5fd6a59dcafdf1edff2915016a2715974756fc5fff66df39");
    CHECK(renderojn::crypto::sha256_hex(profile.package_sha256) ==
          "d60f4b4beac2bf8638ceaa589ca4d08880718ee7a7e166417b3861d3fa31da30");

    const auto make_chart = [&profile](renderojn::format::Difficulty difficulty) {
        renderojn::format::Chart chart;
        chart.difficulty = difficulty;
        chart.header.duration_seconds[static_cast<std::size_t>(difficulty)] = 10;
        chart.notes.reserve(profile.expected_timeline_event_count);
        chart.notes.push_back({120000, profile.expected_reference_id, profile.expected_note_type, profile.expected_measure,
                               profile.expected_slot_index, profile.expected_slot_count});
        for (std::size_t index = 1; index < profile.expected_timeline_event_count; ++index) {
            chart.notes.push_back({130000U + index, static_cast<std::uint16_t>(index), 0, 1, 0, 1});
        }
        return chart;
    };

    const auto original_ojn = std::vector<std::uint8_t>{'o', 'j', 'n'};
    const auto original_package = std::vector<std::uint8_t>{'o', 'j', 'm'};
    const renderojn::io::ByteBuffer ojn(original_ojn);
    const renderojn::io::ByteBuffer package(original_package);

    auto no_match = make_chart(renderojn::format::Difficulty::Hard);
    renderojn::Diagnostics no_match_diagnostics;
    renderojn::compat::apply_known_ojn_ojm_profiles(no_match, ojn, package, no_match_diagnostics);
    CHECK(no_match.notes.front().frame == 120000);
    CHECK(no_match_diagnostics.warnings().empty());
    CHECK(ojn.bytes() == original_ojn);
    CHECK(package.bytes() == original_package);

    auto one_hash_only = make_chart(renderojn::format::Difficulty::Hard);
    auto wrong_package_hash = profile.package_sha256;
    ++wrong_package_hash.back();
    renderojn::Diagnostics one_hash_diagnostics;
    renderojn::compat::apply_ojn_ojm_profile(one_hash_only, profile.ojn_sha256, wrong_package_hash, profile, one_hash_diagnostics);
    CHECK(one_hash_only.notes.front().frame == 120000);
    CHECK(one_hash_diagnostics.warnings().empty());

    for (const auto difficulty : {renderojn::format::Difficulty::Easy, renderojn::format::Difficulty::Normal,
                                  renderojn::format::Difficulty::Hard}) {
        auto exact_pair = make_chart(difficulty);
        renderojn::Diagnostics exact_pair_diagnostics;
        renderojn::compat::apply_ojn_ojm_profile(exact_pair, profile.ojn_sha256, profile.package_sha256, profile, exact_pair_diagnostics);
        const auto corrected = std::find_if(exact_pair.notes.begin(), exact_pair.notes.end(), [&profile](const auto& event) {
            return event.reference_id == profile.expected_reference_id && event.note_type == profile.expected_note_type &&
                   event.measure == profile.expected_measure && event.slot_index == profile.expected_slot_index &&
                   event.slot_count == profile.expected_slot_count;
        });
        REQUIRE(corrected != exact_pair.notes.end());
        CHECK(corrected->frame == 120000 + profile.frame_delay);
        CHECK(exact_pair.notes[1].frame == 130001);
        REQUIRE(exact_pair_diagnostics.warnings().size() == 1);
    }

    auto profile_drift = make_chart(renderojn::format::Difficulty::Hard);
    profile_drift.notes.pop_back();
    renderojn::Diagnostics drift_diagnostics;
    CHECK_THROWS_WITH(renderojn::compat::apply_ojn_ojm_profile(profile_drift, profile.ojn_sha256,
                                                                profile.package_sha256, profile, drift_diagnostics),
                      Catch::Matchers::ContainsSubstring("compatibility profile drift"));
}

TEST_CASE("memory virtual seek checks signed boundary arithmetic") {
    using renderojn::audio::detail::checked_memory_seek_position;
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    const auto minimum = std::numeric_limits<std::int64_t>::min();

    CHECK(checked_memory_seek_position(10, -10, 10) == 0);
    CHECK(checked_memory_seek_position(9, 1, 10) == 10);
    CHECK_FALSE(checked_memory_seek_position(9, -10, 10));
    CHECK_FALSE(checked_memory_seek_position(0, minimum, 10));
    CHECK_FALSE(checked_memory_seek_position(maximum, 1, maximum));
    CHECK_FALSE(checked_memory_seek_position(-1, 0, 10));
    CHECK_FALSE(checked_memory_seek_position(0, 0, -1));
}

TEST_CASE("Ogg Vorbis page normalization preserves valid bytes and repairs only CRC fields") {
    const auto single_bytes = renderojn::test_fixture::ogg_vorbis_stream(false);
    const renderojn::format::EncodedSample single_sample{1001, single_bytes};
    renderojn::Diagnostics single_diagnostics;
    const auto single = renderojn::audio::normalize_ogg_vorbis_pages(single_sample, single_diagnostics);
    CHECK(single.sample_id == 1001);
    CHECK_FALSE(single.repaired());
    CHECK(&single.bytes() == &single_sample.bytes);
    CHECK(single_diagnostics.warnings().empty());

    const auto valid_multi = renderojn::test_fixture::ogg_vorbis_stream(true);
    const renderojn::format::EncodedSample valid_multi_sample{1001, valid_multi};
    renderojn::Diagnostics valid_multi_diagnostics;
    const auto multi = renderojn::audio::normalize_ogg_vorbis_pages(valid_multi_sample, valid_multi_diagnostics);
    CHECK_FALSE(multi.repaired());
    CHECK(&multi.bytes() == &valid_multi_sample.bytes);
    CHECK(valid_multi_diagnostics.warnings().empty());

    auto one_bad_crc = valid_multi;
    constexpr std::size_t kFirstDataPageOffset = 105U;
    one_bad_crc[kFirstDataPageOffset + 22U] ^= 0x80U;
    const renderojn::format::EncodedSample repaired_sample{1001, one_bad_crc};
    renderojn::Diagnostics repaired_diagnostics;
    const auto repaired = renderojn::audio::normalize_ogg_vorbis_pages(repaired_sample, repaired_diagnostics);
    CHECK(repaired.repaired());
    CHECK(repaired.repaired_page_count == 1U);
    CHECK(repaired_sample.bytes == one_bad_crc);
    for (std::size_t index = 0; index < 4U; ++index) {
        CHECK(repaired.bytes()[kFirstDataPageOffset + 22U + index] == valid_multi[kFirstDataPageOffset + 22U + index]);
    }
    REQUIRE(repaired_diagnostics.warnings().size() == 1U);
    CHECK(repaired_diagnostics.warnings().front() == "repaired 1 legacy Ogg page checksum(s) for sample 1001");

    auto two_bad_crcs = valid_multi;
    two_bad_crcs[kFirstDataPageOffset + 22U] ^= 0x80U;
    constexpr std::size_t kFinalDataPageOffset = 134U;
    two_bad_crcs[kFinalDataPageOffset + 25U] ^= 0x40U;
    const renderojn::format::EncodedSample multi_repair_sample{1001, two_bad_crcs};
    renderojn::Diagnostics multi_repair_diagnostics;
    const auto multi_repair = renderojn::audio::normalize_ogg_vorbis_pages(multi_repair_sample, multi_repair_diagnostics);
    CHECK(multi_repair.repaired_page_count == 2U);
    REQUIRE(multi_repair_diagnostics.warnings().size() == 1U);
    CHECK(multi_repair_diagnostics.warnings().front() == "repaired 2 legacy Ogg page checksum(s) for sample 1001");
}

TEST_CASE("Ogg Vorbis normalization rejects malformed page layouts and header-page CRC repairs") {
    const auto complete = renderojn::test_fixture::ogg_vorbis_stream(true);
    const auto normalize = [](std::vector<std::uint8_t> bytes) {
        renderojn::Diagnostics diagnostics;
        return renderojn::audio::normalize_ogg_vorbis_pages({1001, std::move(bytes)}, diagnostics);
    };
    for (std::size_t length = 0; length < complete.size(); ++length) {
        CHECK_THROWS_AS(normalize(std::vector<std::uint8_t>(complete.begin(), complete.begin() + static_cast<std::ptrdiff_t>(length))), renderojn::Error);
    }

    auto bad_capture = complete;
    bad_capture[0] = 'X';
    CHECK_THROWS_AS(normalize(std::move(bad_capture)), renderojn::Error);

    auto unsupported_version = complete;
    unsupported_version[4] = 1;
    CHECK_THROWS_AS(normalize(std::move(unsupported_version)), renderojn::Error);

    auto bad_lacing = complete;
    bad_lacing[27] = 255;
    CHECK_THROWS_AS(normalize(std::move(bad_lacing)), renderojn::Error);

    constexpr std::size_t kSecondPageOffset = 35U;
    auto wrong_sequence = complete;
    ++wrong_sequence[kSecondPageOffset + 18U];
    CHECK_THROWS_AS(normalize(std::move(wrong_sequence)), renderojn::Error);

    auto wrong_serial = complete;
    ++wrong_serial[kSecondPageOffset + 14U];
    CHECK_THROWS_AS(normalize(std::move(wrong_serial)), renderojn::Error);

    auto repeated_bos = complete;
    repeated_bos[kSecondPageOffset + 5U] = 0x02U;
    CHECK_THROWS_AS(normalize(std::move(repeated_bos)), renderojn::Error);

    auto unexpected_continuation = complete;
    unexpected_continuation[kSecondPageOffset + 5U] = 0x01U;
    CHECK_THROWS_AS(normalize(std::move(unexpected_continuation)), renderojn::Error);

    auto trailing_junk = complete;
    trailing_junk.push_back(0);
    CHECK_THROWS_AS(normalize(std::move(trailing_junk)), renderojn::Error);

    auto bos_crc = complete;
    bos_crc[22] ^= 0x01U;
    CHECK_THROWS_WITH(normalize(std::move(bos_crc)), Catch::Matchers::ContainsSubstring("header page"));
}

TEST_CASE("CLI accepts help anywhere and rejects invalid values") {
    CHECK(renderojn::app::parse_cli({"song.ojn", "--help"}).help);
    CHECK(renderojn::app::parse_cli({"--wat", "--help"}).help);
    CHECK_THROWS_AS(renderojn::app::parse_cli({"song.ojn", "--difficulty", "H"}), renderojn::Error);
    CHECK_THROWS_AS(renderojn::app::parse_cli({"song.ojn", "--quality", "4"}), renderojn::Error);
    CHECK_THROWS_AS(renderojn::app::parse_cli({"song.ojn", "--wat"}), renderojn::Error);
}

TEST_CASE("CLI output extensions are exact and never duplicated") {
    const auto plain = renderojn::app::parse_cli({"folder/song.ojn", "--format", "wav"});
    CHECK(renderojn::app::resolve_output_path(plain).extension() == ".wav");
    const auto named = renderojn::app::parse_cli({"song.ojn", "--format", "wav", "--outfile", "mix.wav"});
    CHECK(renderojn::app::resolve_output_path(named).filename() == "mix.wav");
    const auto unextended = renderojn::app::parse_cli({"song.ojn", "--format", "ogg", "--outfile", "mix"});
    CHECK(renderojn::app::resolve_output_path(unextended).filename() == "mix.ogg");
    CHECK_THROWS_AS(renderojn::app::resolve_output_path(renderojn::app::parse_cli({"song.ojn", "--format", "wav", "--outfile", "mix.mp3"})), renderojn::Error);
}

TEST_CASE("checked arithmetic and bounded reads reject overflow and truncation") {
    std::size_t value{};
    CHECK_FALSE(renderojn::io::checked_add(static_cast<std::size_t>(-1), 1, value));
    CHECK_FALSE(renderojn::io::checked_multiply(static_cast<std::size_t>(-1), 2, value));
    auto bytes = std::make_shared<renderojn::io::ByteBuffer>(std::vector<std::uint8_t>{1});
    renderojn::io::ByteReader reader(bytes);
    CHECK_THROWS_AS(reader.u32le("unit value"), renderojn::Error);
}

TEST_CASE("ordinary OJN header and tuple are parsed from one immutable buffer") {
    const auto source = renderojn::test_fixture::ordinary_ojn();
    const auto chart = renderojn::format::parse_ojn_chart(source, renderojn::format::Difficulty::Hard);
    REQUIRE(chart.header.title == "Synthetic Title");
    REQUIRE(chart.header.artist == "Synthetic Artist");
    REQUIRE(chart.notes.size() == 1);
    CHECK(chart.notes.front().reference_id == 1);
    CHECK(chart.notes.front().frame == 0);
}

TEST_CASE("OJN preserves exact five-way source positions before frame conversion") {
    renderojn::test_fixture::OrdinaryOjnSpec spec;
    spec.durations = {{3, 3, 3}};
    spec.counts[2] = {1, 1, 0, 1};
    spec.hard_packages = {{0, 2, 5,
                           {renderojn::test_fixture::ojn_record(0), renderojn::test_fixture::ojn_record(0),
                            renderojn::test_fixture::ojn_record(0), renderojn::test_fixture::ojn_record(0),
                            renderojn::test_fixture::ojn_record(1)}}};

    const auto chart = renderojn::format::parse_ojn_chart(renderojn::test_fixture::ordinary_ojn(spec), renderojn::format::Difficulty::Hard);
    REQUIRE(chart.notes.size() == 1);
    CHECK(chart.notes.front().measure == 0);
    CHECK(chart.notes.front().slot_index == 4);
    CHECK(chart.notes.front().slot_count == 5);
    CHECK(chart.notes.front().frame == 76800);
}

TEST_CASE("OJN applies channel-zero fractional measures to later positions") {
    renderojn::test_fixture::OrdinaryOjnSpec spec;
    spec.durations = {{3, 3, 3}};
    spec.counts[2] = {1, 1, 1, 2};
    spec.hard_packages = {{0, 0, 1, {renderojn::test_fixture::ojn_scalar_record(0.5F)}},
                          {1, 2, 1, {renderojn::test_fixture::ojn_record(1)}}};

    const auto chart = renderojn::format::parse_ojn_chart(renderojn::test_fixture::ordinary_ojn(spec), renderojn::format::Difficulty::Hard);
    REQUIRE(chart.notes.size() == 1);
    CHECK(chart.notes.front().measure == 1);
    CHECK(chart.notes.front().slot_index == 0);
    CHECK(chart.notes.front().slot_count == 1);
    CHECK(chart.notes.front().frame == 48000);
}

TEST_CASE("OJN integrates BPM changes in chronological order rather than channel order") {
    renderojn::test_fixture::OrdinaryOjnSpec spec;
    spec.durations = {{3, 3, 3}};
    spec.counts[2] = {2, 2, 0, 2};
    spec.hard_packages = {{0, 1, 4,
                           {renderojn::test_fixture::ojn_scalar_record(0.0F), renderojn::test_fixture::ojn_scalar_record(0.0F),
                            renderojn::test_fixture::ojn_scalar_record(60.0F), renderojn::test_fixture::ojn_scalar_record(0.0F)}},
                          {0, 2, 4,
                           {renderojn::test_fixture::ojn_record(0), renderojn::test_fixture::ojn_record(1),
                            renderojn::test_fixture::ojn_record(0), renderojn::test_fixture::ojn_record(2)}}};

    const auto chart = renderojn::format::parse_ojn_chart(renderojn::test_fixture::ordinary_ojn(spec), renderojn::format::Difficulty::Hard);
    REQUIRE(chart.notes.size() == 2);
    CHECK(chart.notes[0].measure == 0);
    CHECK(chart.notes[0].slot_index == 1);
    CHECK(chart.notes[0].slot_count == 4);
    CHECK(chart.notes[0].frame == 24000);
    CHECK(chart.notes[1].measure == 0);
    CHECK(chart.notes[1].slot_index == 3);
    CHECK(chart.notes[1].slot_count == 4);
    CHECK(chart.notes[1].frame == 96000);
}

TEST_CASE("OJN header counts bound package parsing and validate chart totals") {
    using renderojn::format::Difficulty;
    using renderojn::test_fixture::OjnEventSet;
    using renderojn::test_fixture::OrdinaryOjnSpec;
    using renderojn::test_fixture::ojn_record;

    const auto parse_hard = [](const OrdinaryOjnSpec& spec) {
        return renderojn::format::parse_ojn_chart(renderojn::test_fixture::ordinary_ojn(spec), Difficulty::Hard);
    };
    const auto malformed_ojn = Catch::Matchers::ContainsSubstring("Malformed OJN:");

    OrdinaryOjnSpec too_few_packages;
    too_few_packages.counts[2] = {1, 1, 0, 1};
    too_few_packages.hard_packages = {{0, 2, 1, {ojn_record(1)}}, {0, 0, 1, {ojn_record(0)}}};
    CHECK_THROWS_WITH(parse_hard(too_few_packages), malformed_ojn);

    OrdinaryOjnSpec too_many_packages;
    too_many_packages.counts[2] = {1, 1, 0, 2};
    too_many_packages.hard_packages = {{0, 2, 1, {ojn_record(1)}}};
    auto truncated_second_package = renderojn::test_fixture::ordinary_ojn(too_many_packages)->bytes();
    renderojn::test_fixture::u32(truncated_second_package, 1);
    renderojn::test_fixture::u16(truncated_second_package, 0);
    renderojn::test_fixture::u16(truncated_second_package, 1);
    const auto truncated_size = static_cast<std::uint32_t>(truncated_second_package.size());
    for (std::size_t shift = 0; shift < 32; shift += 8) truncated_second_package[296 + shift / 8] = static_cast<std::uint8_t>(truncated_size >> shift);
    CHECK_THROWS_WITH(renderojn::format::parse_ojn_chart(
                          std::make_shared<renderojn::io::ByteBuffer>(std::move(truncated_second_package)), Difficulty::Hard),
                      malformed_ojn);

    OrdinaryOjnSpec event_count_mismatch;
    event_count_mismatch.counts[2] = {2, 1, 0, 1};
    event_count_mismatch.hard_packages = {{0, 2, 1, {ojn_record(1)}}};
    CHECK_THROWS_WITH(parse_hard(event_count_mismatch), malformed_ojn);

    OrdinaryOjnSpec note_count_mismatch;
    note_count_mismatch.counts[2] = {1, 0, 0, 1};
    note_count_mismatch.hard_packages = {{0, 2, 1, {ojn_record(1)}}};
    CHECK_THROWS_WITH(parse_hard(note_count_mismatch), malformed_ojn);

    OrdinaryOjnSpec measure_exceeds_header;
    measure_exceeds_header.counts[2] = {1, 1, 0, 1};
    measure_exceeds_header.hard_packages = {{1, 2, 1, {ojn_record(1)}}};
    CHECK_THROWS_WITH(parse_hard(measure_exceeds_header), malformed_ojn);

    OrdinaryOjnSpec empty_final_measure;
    empty_final_measure.counts[2] = {1, 1, 2, 1};
    empty_final_measure.hard_packages = {{0, 2, 1, {ojn_record(1)}}};
    CHECK_NOTHROW(parse_hard(empty_final_measure));

    OrdinaryOjnSpec impossible_package_count;
    impossible_package_count.counts[2] = {0, 0, 0, 2};
    CHECK_THROWS_WITH(parse_hard(impossible_package_count), malformed_ojn);

    OrdinaryOjnSpec excessive_event_count;
    excessive_event_count.counts[2] = {renderojn::format::kMaxEvents + 1U, 0, 0, 0};
    CHECK_THROWS_WITH(parse_hard(excessive_event_count), malformed_ojn);

    OrdinaryOjnSpec excessive_note_count;
    excessive_note_count.counts[2] = {0, renderojn::format::kMaxEvents + 1U, 0, 0};
    CHECK_THROWS_WITH(parse_hard(excessive_note_count), malformed_ojn);
}

TEST_CASE("OJN validates timing scalars and applies same-position BPM changes before notes") {
    using renderojn::format::Difficulty;
    using renderojn::test_fixture::OrdinaryOjnSpec;
    using renderojn::test_fixture::ojn_record;
    using renderojn::test_fixture::ojn_scalar_record;

    const auto parse_hard = [](const OrdinaryOjnSpec& spec) {
        return renderojn::format::parse_ojn_chart(renderojn::test_fixture::ordinary_ojn(spec), Difficulty::Hard);
    };

    OrdinaryOjnSpec negative_fraction;
    negative_fraction.counts[2] = {0, 0, 0, 1};
    negative_fraction.hard_packages = {{0, 0, 1, {ojn_scalar_record(-0.5F)}}};
    CHECK_THROWS_AS(parse_hard(negative_fraction), renderojn::Error);

    OrdinaryOjnSpec nonfinite_fraction;
    nonfinite_fraction.counts[2] = {0, 0, 0, 1};
    nonfinite_fraction.hard_packages = {{0, 0, 1, {ojn_scalar_record(std::numeric_limits<float>::infinity())}}};
    CHECK_THROWS_AS(parse_hard(nonfinite_fraction), renderojn::Error);

    OrdinaryOjnSpec negative_tempo;
    negative_tempo.counts[2] = {0, 0, 0, 1};
    negative_tempo.hard_packages = {{0, 1, 1, {ojn_scalar_record(-60.0F)}}};
    CHECK_THROWS_AS(parse_hard(negative_tempo), renderojn::Error);

    OrdinaryOjnSpec nonfinite_tempo;
    nonfinite_tempo.counts[2] = {0, 0, 0, 1};
    nonfinite_tempo.hard_packages = {{0, 1, 1, {ojn_scalar_record(std::numeric_limits<float>::infinity())}}};
    CHECK_THROWS_AS(parse_hard(nonfinite_tempo), renderojn::Error);

    OrdinaryOjnSpec zero_timing_padding;
    zero_timing_padding.counts[2] = {1, 1, 0, 3};
    zero_timing_padding.hard_packages = {{0, 0, 1, {ojn_scalar_record(0.0F)}}, {0, 1, 1, {ojn_scalar_record(0.0F)}},
                                         {0, 2, 1, {ojn_record(1)}}};
    CHECK_NOTHROW(parse_hard(zero_timing_padding));

    OrdinaryOjnSpec same_position_tempos;
    same_position_tempos.durations = {{3, 3, 3}};
    same_position_tempos.counts[2] = {1, 1, 0, 3};
    same_position_tempos.hard_packages = {{0, 1, 1, {ojn_scalar_record(60.0F)}}, {0, 1, 1, {ojn_scalar_record(30.0F)}},
                                           {0, 2, 4, {ojn_record(0), ojn_record(1), ojn_record(0), ojn_record(0)}}};
    const auto chronological = parse_hard(same_position_tempos);
    REQUIRE(chronological.notes.size() == 1);
    CHECK(chronological.notes.front().frame == 96000);

    // Declared duration is song-list metadata, not a content bound: real charts
    // routinely place notes past it (30 of 229 in the O2Jam/O2Jam Thai corpus).
    // Neither Open2Jam nor CXO2 validates events against it.
    OrdinaryOjnSpec beyond_duration;
    beyond_duration.durations = {{0, 0, 0}};
    beyond_duration.counts[2] = {1, 1, 1, 1};
    beyond_duration.hard_packages = {{1, 2, 1, {ojn_record(1)}}};
    CHECK_NOTHROW(parse_hard(beyond_duration));
}

TEST_CASE("OJN permits notes in the terminal output second after declared duration") {
    using renderojn::format::Difficulty;
    using renderojn::test_fixture::OrdinaryOjnSpec;
    using renderojn::test_fixture::ojn_record;

    OrdinaryOjnSpec spec;
    spec.durations = {{1, 1, 1}};
    spec.counts[2] = {1, 1, 0, 1};
    spec.hard_packages = {{0, 2, 4, {ojn_record(0), ojn_record(0), ojn_record(0), ojn_record(1)}}};

    const auto chart = renderojn::format::parse_ojn_chart(renderojn::test_fixture::ordinary_ojn(spec), Difficulty::Hard);
    REQUIRE(chart.notes.size() == 1);
    CHECK(chart.notes.front().frame == 72000);
}

TEST_CASE("OJN accepts notes well past the declared duration") {
    using renderojn::format::Difficulty;
    using renderojn::test_fixture::OrdinaryOjnSpec;
    using renderojn::test_fixture::ojn_record;

    // Mirrors o2ma105.ojn: the hard chart declares 97s but its last note lands
    // at ~101s. At 120 BPM a note at measure 1 is 8s in, far beyond a 1s
    // declaration, and must parse rather than fail.
    OrdinaryOjnSpec spec;
    spec.durations = {{1, 1, 1}};
    spec.counts[2] = {1, 1, 1, 1};
    spec.hard_packages = {{1, 2, 1, {ojn_record(1)}}};

    const auto chart = renderojn::format::parse_ojn_chart(renderojn::test_fixture::ordinary_ojn(spec), Difficulty::Hard);
    REQUIRE(chart.notes.size() == 1);
    CHECK(chart.notes.front().frame == 96000);
}

TEST_CASE("OJN still rejects events beyond the six-hour safety cap") {
    using renderojn::format::Difficulty;
    using renderojn::test_fixture::OrdinaryOjnSpec;
    using renderojn::test_fixture::ojn_record;

    // Relaxing the declared-duration bound must not weaken the absolute cap
    // that protects against hostile input. At 120 BPM one measure is 2s, so a
    // measure index beyond 10,800 measures exceeds six hours.
    OrdinaryOjnSpec spec;
    spec.durations = {{1, 1, 1}};
    spec.counts[2] = {1, 1, 20000, 1};
    spec.hard_packages = {{20000, 2, 1, {ojn_record(1)}}};

    CHECK_THROWS_AS(renderojn::format::parse_ojn_chart(renderojn::test_fixture::ordinary_ojn(spec), Difficulty::Hard),
                    renderojn::Error);
}

TEST_CASE("the six-hour cap is enforced at its exact boundary") {
    using renderojn::format::Difficulty;
    using renderojn::test_fixture::OrdinaryOjnSpec;
    using renderojn::test_fixture::ojn_record;

    // Since the declared-duration bound was removed, this cap is the only guard
    // between malformed input and unbounded work, so pin it at the boundary
    // rather than only far past it. At 120 BPM a measure is exactly 2 seconds,
    // so measure 10,800 is exactly six hours (the exclusive limit) and measure
    // 10,799 is two seconds inside it.
    const auto parse_at_measure = [](std::uint32_t measure) {
        OrdinaryOjnSpec spec;
        spec.durations = {{1, 1, 1}};
        spec.counts[2] = {1, 1, measure, 1};
        spec.hard_packages = {{measure, 2, 1, {ojn_record(1)}}};
        return renderojn::format::parse_ojn_chart(renderojn::test_fixture::ordinary_ojn(spec), Difficulty::Hard);
    };

    const auto inside = parse_at_measure(10799U);
    REQUIRE(inside.notes.size() == 1);
    CHECK(inside.notes.front().frame == 10799ULL * 2ULL * 48000ULL);

    CHECK_THROWS_AS(parse_at_measure(10800U), renderojn::Error);
}

TEST_CASE("output length covers notes past the declared duration") {
    using renderojn::format::Difficulty;
    using renderojn::test_fixture::OrdinaryOjnSpec;
    using renderojn::test_fixture::ojn_record;

    OrdinaryOjnSpec spec;
    spec.durations = {{1, 1, 1}};
    spec.counts[2] = {1, 1, 1, 1};
    spec.hard_packages = {{1, 2, 1, {ojn_record(1)}}};

    const auto chart = renderojn::format::parse_ojn_chart(renderojn::test_fixture::ordinary_ojn(spec), Difficulty::Hard);
    // The note sits at frame 96,000 (2s). A duration-only buffer would be
    // 2 * 48,000 = 96,000 frames and would clip it entirely.
    CHECK(renderojn::render::output_frame_count(chart) > chart.notes.front().frame);
}

TEST_CASE("truncated OJN headers fail explicitly") {
    auto truncated = std::make_shared<renderojn::io::ByteBuffer>(std::vector<std::uint8_t>(299));
    CHECK_THROWS_AS(renderojn::format::parse_ojn_header(truncated), renderojn::Error);
}

TEST_CASE("genre codes resolve to their names and out-of-range codes warn") {
    // Both the CLI and the WebAssembly binding tag output with this, so it is
    // shared rather than copied; the table itself is fixed by the format.
    renderojn::Diagnostics diagnostics;
    CHECK(renderojn::format::genre_name(0, diagnostics) == "Ballad");
    CHECK(renderojn::format::genre_name(2, diagnostics) == "Dance");
    CHECK(renderojn::format::genre_name(10, diagnostics) == "Etc");
    CHECK(diagnostics.warnings().empty());

    // Out of range falls back to Etc rather than throwing: a bad genre code is
    // not a reason to refuse to render a chart.
    CHECK(renderojn::format::genre_name(11, diagnostics) == "Etc");
    CHECK(diagnostics.warnings().size() == 1);
    CHECK(renderojn::format::genre_name(4294967295U, diagnostics) == "Etc");
    CHECK(diagnostics.warnings().size() == 2);
}

TEST_CASE("Korea-era new wrappers decrypt to the ordinary chart they contain") {
    using renderojn::format::Difficulty;

    // Round trip: the fixture encrypts, the parser decrypts. The decrypted chart
    // must be indistinguishable from the plain one it was built from.
    const auto plain = renderojn::test_fixture::ordinary_ojn();
    const auto wrapped = renderojn::test_fixture::new_wrapped_ojn(plain);
    REQUIRE(wrapped->size() == plain->size() + 8U);

    const auto normalized = renderojn::format::normalize_ojn(wrapped);
    CHECK(normalized->bytes() == plain->bytes());

    const auto expected = renderojn::format::parse_ojn_chart(plain, Difficulty::Hard);
    const auto actual = renderojn::format::parse_ojn_chart(wrapped, Difficulty::Hard);
    CHECK(actual.header.song_id == expected.header.song_id);
    CHECK(actual.header.title == expected.header.title);
    REQUIRE(actual.notes.size() == expected.notes.size());
    CHECK(actual.notes.front().frame == expected.notes.front().frame);
}

TEST_CASE("new wrapper decryption covers every block size the corpus uses") {
    // Real files use block sizes 4 through 11 and the mid-key lands at
    // blockSize/2, so an off-by-one in that index would corrupt only some sizes.
    // Cover the observed range with a margin on either side.
    //
    // Sizes 1 and 2 are deliberately excluded. At size 1 the mid-key overwrites
    // the initial key and at size 2 it lands on index 1, so the fixture and the
    // parser would agree with each other by construction rather than against the
    // format. No corpus file uses a block size below 4, so such a test would
    // assert self-consistency and prove nothing.
    for (std::uint8_t block_size = 3; block_size <= 12U; ++block_size) {
        const auto plain = renderojn::test_fixture::ordinary_ojn();
        const auto wrapped = renderojn::test_fixture::new_wrapped_ojn(plain, block_size);
        const auto normalized = renderojn::format::normalize_ojn(wrapped);
        CHECK(normalized->bytes() == plain->bytes());
    }
}

TEST_CASE("new wrapper decryption matches real corpus key parameters") {
    // Guards the key layout against the actual observed parameters rather than
    // against the fixture's mirror of the implementation: block size 11 with
    // main 0x46, mid 0xe1 and initial 0x85 are the values carried by a real
    // NOWCOM chart. Byte 0 must come from the initial key, and byte 5 from the
    // mid key because 11 / 2 == 5.
    const auto plain = renderojn::test_fixture::ordinary_ojn();
    const auto wrapped = renderojn::test_fixture::new_wrapped_ojn(plain, 11U, 0x46U, 0xe1U, 0x85U);
    const auto& raw = wrapped->bytes();

    REQUIRE(raw.size() > 8U);
    CHECK(raw[3] == 11U);
    CHECK(raw[4] == 0x46U);
    CHECK(raw[5] == 0xe1U);
    CHECK(raw[6] == 0x85U);

    // Decrypting by hand, independent of the parser.
    CHECK(static_cast<std::uint8_t>(raw[raw.size() - 1U] ^ 0x85U) == plain->bytes()[0]);
    CHECK(static_cast<std::uint8_t>(raw[raw.size() - 6U] ^ 0xe1U) == plain->bytes()[5]);
}

TEST_CASE("normalizing an ordinary OJN twice is a no-op even when it starts with 'new'") {
    using renderojn::format::Difficulty;

    // song_id 0x77656E puts the bytes 'n','e','w' at offsets 0-2 of a perfectly
    // ordinary chart. parse_ojn_chart normalizes and then hands the result to
    // parse_ojn_header, which normalizes again, so without a signature-first
    // check that chart would be decrypted a second time and rejected.
    auto bytes = renderojn::test_fixture::ordinary_ojn()->bytes();
    REQUIRE(bytes.size() > 8U);
    bytes[0] = 'n'; bytes[1] = 'e'; bytes[2] = 'w'; bytes[3] = 0;
    auto disguised = std::make_shared<renderojn::io::ByteBuffer>(std::move(bytes));

    const auto normalized = renderojn::format::normalize_ojn(disguised);
    CHECK(normalized->bytes() == disguised->bytes());
    CHECK(renderojn::format::normalize_ojn(normalized)->bytes() == disguised->bytes());
    CHECK_NOTHROW(renderojn::format::parse_ojn_chart(disguised, Difficulty::Hard));
}

TEST_CASE("malformed new wrappers fail instead of feeding garbage to the parser") {
    using renderojn::test_fixture::new_wrapped_ojn;
    using renderojn::test_fixture::ordinary_ojn;

    // A zero block size would divide by zero / index an empty key.
    auto zero_block = new_wrapped_ojn(ordinary_ojn(), 0U);
    CHECK_THROWS_AS(renderojn::format::normalize_ojn(zero_block), renderojn::Error);

    // Too short to carry the 8-byte key header.
    auto stub = std::make_shared<renderojn::io::ByteBuffer>(std::vector<std::uint8_t>{'n', 'e', 'w', 11});
    CHECK_THROWS_AS(renderojn::format::normalize_ojn(stub), renderojn::Error);

    // Correct container, wrong key: the decrypted bytes are not an OJN, and that
    // must be rejected rather than parsed as though the key had worked.
    const auto plain = ordinary_ojn();
    auto wrong_key = new_wrapped_ojn(plain, 11U, 0x46U, 0xe1U, 0x85U);
    auto corrupted = wrong_key->bytes();
    corrupted[4] = static_cast<std::uint8_t>(corrupted[4] ^ 0xffU); // flip mainKey
    auto mismatched = std::make_shared<renderojn::io::ByteBuffer>(std::move(corrupted));
    CHECK_THROWS_AS(renderojn::format::normalize_ojn(mismatched), renderojn::Error);
}

TEST_CASE("every ordinary OJN header truncation boundary fails") {
    const auto complete = renderojn::test_fixture::ordinary_ojn()->bytes();
    for (std::size_t length = 0; length < 300; ++length) {
        auto truncated = std::make_shared<renderojn::io::ByteBuffer>(std::vector<std::uint8_t>(complete.begin(), complete.begin() + static_cast<std::ptrdiff_t>(length)));
        CHECK_THROWS_AS(renderojn::format::parse_ojn_header(truncated), renderojn::Error);
    }
}

TEST_CASE("M30 nami samples retain their exact mapped ids") {
    const auto package = renderojn::format::parse_sample_package(renderojn::test_fixture::m30_flag16());
    REQUIRE(package.samples.size() == 1);
    CHECK(package.samples.front().id == 1);
    CHECK(package.samples.front().bytes == renderojn::test_fixture::ogg_payload());
}

TEST_CASE("M30 nami decoding leaves the trailing partial group untouched") {
    // `nami` XORs complete four-byte groups only; the final 0-3 bytes are
    // stored verbatim.  A six-byte payload proves the two-byte tail survives.
    const std::vector<std::uint8_t> decoded{'O', 'g', 'g', 'S', 0x11, 0x22};
    const auto package = renderojn::format::parse_sample_package(
        renderojn::test_fixture::m30_package(16U, decoded));
    REQUIRE(package.samples.size() == 1);
    CHECK(package.samples.front().bytes == decoded);
}

TEST_CASE("M30 plaintext flags preserve payload bytes exactly") {
    // Flags 0 and 32 are plaintext Ogg variants in 1.0.0.  CXO2 transforms only
    // flag 16, and every installed flag-0 sample already begins with `OggS`.
    const std::vector<std::uint8_t> decoded{'O', 'g', 'g', 'S', 0x01, 0x02, 0x03, 0x04, 0x05};
    for (const std::uint32_t flag : {0U, 32U}) {
        const auto package = renderojn::format::parse_sample_package(
            renderojn::test_fixture::m30_package(flag, decoded));
        REQUIRE(package.samples.size() == 1);
        CHECK(package.samples.front().id == 1);
        CHECK(package.samples.front().bytes == decoded);
    }
}

TEST_CASE("M30 codec codes map only documented normal and background banks") {
    const auto normal = renderojn::format::parse_sample_package(renderojn::test_fixture::m30_flag16(renderojn::test_fixture::ogg_payload(), 5));
    REQUIRE(normal.samples.size() == 1);
    CHECK(normal.samples.front().id == 1);

    const auto background = renderojn::format::parse_sample_package(renderojn::test_fixture::m30_flag16(renderojn::test_fixture::ogg_payload(), 0));
    REQUIRE(background.samples.size() == 1);
    CHECK(background.samples.front().id == 1001);

    CHECK_THROWS_WITH(renderojn::format::parse_sample_package(renderojn::test_fixture::m30_flag16(renderojn::test_fixture::ogg_payload(), 1)),
                      Catch::Matchers::ContainsSubstring("codec 1"));
}

TEST_CASE("unsupported M30 flags fail rather than omitting samples") {
    for (const std::uint32_t flag : {1U, 2U, 4U, 8U, 17U, 64U}) {
        CHECK_THROWS_AS(renderojn::format::parse_sample_package(
                            renderojn::test_fixture::m30_package(flag)),
                        renderojn::Error);
    }
}

TEST_CASE("M30 payloads that are not Ogg streams fail explicitly") {
    // A decoded M30 sample must be a real Ogg stream.  Anything else means the
    // flag was misidentified, so it must fail instead of reaching the decoder.
    for (const std::uint32_t flag : {0U, 16U, 32U}) {
        CHECK_THROWS_AS(renderojn::format::parse_sample_package(
                            renderojn::test_fixture::m30_package(flag, renderojn::test_fixture::riff_bytes())),
                        renderojn::Error);
    }
}

TEST_CASE("declared empty sample payloads fail rather than being silently omitted") {
    CHECK_THROWS_AS(renderojn::format::parse_sample_package(
                        renderojn::test_fixture::m30_flag16(std::vector<std::uint8_t>{})),
                    renderojn::Error);
}

TEST_CASE("OJM accepts complete zero-byte Ogg sentinel records") {
    const auto package = renderojn::format::parse_sample_package(renderojn::test_fixture::ojm_with_empty_ogg_sentinel());
    REQUIRE(package.samples.size() == 1);
    CHECK(package.samples.front().id == 1001);
    CHECK(package.samples.front().bytes == std::vector<std::uint8_t>{'O', 'g', 'g', 'S'});
    CHECK(package.empty_ogg_sentinel_records == 1);
}

TEST_CASE("OMC and OJM preserve positional sample ids across empty PCM slots") {
    const std::vector<std::uint8_t> payload(17, 0x11U);
    for (const auto& signature : {std::string("OMC"), std::string("OJM")}) {
        const auto package = renderojn::format::parse_sample_package(
            renderojn::test_fixture::omc_or_ojm_with_slots(signature, {{}, payload}, {}));
        REQUIRE(package.samples.size() == 1);
        CHECK(package.samples.front().id == 2);
        CHECK(package.empty_pcm_slot_records == 1);
    }
}

TEST_CASE("OMC and OJM preserve positional sample ids across empty declared Ogg slots") {
    for (const auto& signature : {std::string("OMC"), std::string("OJM")}) {
        const auto package = renderojn::format::parse_sample_package(
            renderojn::test_fixture::omc_or_ojm_with_slots(signature, {}, {{}, renderojn::test_fixture::ogg_signature()}));
        REQUIRE(package.samples.size() == 1);
        CHECK(package.samples.front().id == 1002);
        CHECK(package.empty_ogg_sentinel_records == 1);
    }
}

TEST_CASE("sample packages accept multiple complete trailing empty Ogg directory slots") {
    for (const auto trailing_count : {2U, 3U}) {
        const auto package = renderojn::format::parse_sample_package(
            renderojn::test_fixture::omc_or_ojm_with_slots("OJM", {}, {renderojn::test_fixture::ogg_signature()}, trailing_count));
        REQUIRE(package.samples.size() == 1);
        CHECK(package.samples.front().id == 1001);
        CHECK(package.empty_ogg_sentinel_records == trailing_count);
    }
}

TEST_CASE("sample packages cap declared and trailing directory slots together") {
    CHECK_THROWS_AS(renderojn::format::parse_sample_package(
                        renderojn::test_fixture::omc_or_ojm_with_slots("OJM", {}, {{}}, renderojn::format::kMaxSamples)),
                    renderojn::Error);
}

TEST_CASE("OMC PCM block permutation places encoded blocks at table-selected plaintext offsets") {
    const auto package = renderojn::format::parse_sample_package(renderojn::test_fixture::omc_with_shuffled_pcm());
    REQUIRE(package.samples.size() == 1);
    const std::vector<std::uint8_t> decoded_payload(package.samples.front().bytes.end() - 17, package.samples.front().bytes.end());
    CHECK(decoded_payload == std::vector<std::uint8_t>{250, 248, 253, 241, 251, 243, 247, 249, 9, 3, 11, 16, 13, 240, 254, 10, 0});
}

TEST_CASE("OJM PCM payloads remain plaintext") {
    const auto package = renderojn::format::parse_sample_package(renderojn::test_fixture::ojm_with_plain_pcm());
    REQUIRE(package.samples.size() == 1);
    const std::vector<std::uint8_t> decoded_payload(package.samples.front().bytes.end() - 17, package.samples.front().bytes.end());
    CHECK(decoded_payload == std::vector<std::uint8_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
}

TEST_CASE("OJM rejects truncated or payload-bearing trailing records") {
    auto payload_bearing = renderojn::test_fixture::ojm_with_empty_ogg_sentinel()->bytes();
    payload_bearing[payload_bearing.size() - 4] = 1;
    CHECK_THROWS_AS(renderojn::format::parse_sample_package(
                        std::make_shared<renderojn::io::ByteBuffer>(std::move(payload_bearing))),
                    renderojn::Error);

    auto truncated = renderojn::test_fixture::ojm_with_empty_ogg_sentinel()->bytes();
    truncated.pop_back();
    CHECK_THROWS_AS(renderojn::format::parse_sample_package(
                        std::make_shared<renderojn::io::ByteBuffer>(std::move(truncated))),
                    renderojn::Error);
}

TEST_CASE("every truncated M30 record fails") {
    const auto complete = renderojn::test_fixture::m30_flag16()->bytes();
    for (std::size_t length = 0; length < complete.size(); ++length) {
        auto truncated = std::make_shared<renderojn::io::ByteBuffer>(std::vector<std::uint8_t>(complete.begin(), complete.begin() + static_cast<std::ptrdiff_t>(length)));
        CHECK_THROWS_AS(renderojn::format::parse_sample_package(truncated), renderojn::Error);
    }
}
