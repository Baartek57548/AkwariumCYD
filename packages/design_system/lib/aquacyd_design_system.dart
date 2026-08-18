library;

import 'package:flutter/material.dart';

abstract final class ProductSpacing {
  static const double xxs = 4;
  static const double xs = 8;
  static const double sm = 12;
  static const double md = 16;
  static const double lg = 24;
  static const double xl = 32;
  static const double xxl = 48;
}

abstract final class ProductRadius {
  static const double control = 16;
  static const double card = 22;
  static const double hero = 28;
  static const double pill = 999;
}

/// Shared dimensions used by every adaptive Home Control surface.
///
/// Keeping breakpoints and minimum targets here prevents individual screens
/// from drifting into subtly different responsive behaviour.
abstract final class ProductLayout {
  static const double minimumTouchTarget = 48;
  static const double pageHorizontalPadding = 20;
  static const double pageTopPadding = 20;
  static const double pageBottomPadding = 100;
  static const double compactBreakpoint = 420;
  static const double twoColumnBreakpoint = 580;
  static const double threeColumnBreakpoint = 900;
  static const double maximumContentWidth = 1280;
}

abstract final class ProductIconSize {
  static const double small = 18;
  static const double medium = 24;
  static const double large = 32;
  static const double hero = 48;
}

abstract final class ProductMotion {
  static const Duration fast = Duration(milliseconds: 120);
  static const Duration standard = Duration(milliseconds: 220);
  static const Duration emphasized = Duration(milliseconds: 320);

  static const Curve enter = Curves.easeOutCubic;
  static const Curve exit = Curves.easeInCubic;
  static const Curve stateChange = Curves.easeInOutCubic;
}

abstract final class ProductElevation {
  static const double flat = 0;
  static const double raised = 1;
  static const double overlay = 3;
}

abstract final class ProductShadows {
  static List<BoxShadow> soft(Color color) => <BoxShadow>[
    BoxShadow(
      color: color.withValues(alpha: 0.12),
      blurRadius: 24,
      offset: const Offset(0, 10),
    ),
  ];

  static List<BoxShadow> floating(Color color) => <BoxShadow>[
    BoxShadow(
      color: color.withValues(alpha: 0.18),
      blurRadius: 32,
      offset: const Offset(0, 16),
    ),
  ];
}

abstract final class ProductColors {
  static const Color aqua = Color(0xFF18C7B5);
  static const Color ocean = Color(0xFF087EA4);
  static const Color indigo = Color(0xFF4254D0);
  static const Color night = Color(0xFF071619);
  static const Color success = Color(0xFF27AE60);
  static const Color warning = Color(0xFFE3A008);
}

final class HomeControlMark extends StatelessWidget {
  const HomeControlMark({required this.size, super.key});

  final double size;

  @override
  Widget build(BuildContext context) {
    return Semantics(
      image: true,
      label: 'Home Control',
      child: CustomPaint(
        size: Size.square(size),
        painter: _HomeControlMarkPainter(),
      ),
    );
  }
}

final class _HomeControlMarkPainter extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) {
    final rect = Offset.zero & size;
    final radius = Radius.circular(size.width * 0.28);
    final background = Paint()
      ..shader = const LinearGradient(
        begin: Alignment.topLeft,
        end: Alignment.bottomRight,
        colors: <Color>[ProductColors.aqua, ProductColors.indigo],
      ).createShader(rect);
    canvas.drawRRect(RRect.fromRectAndRadius(rect, radius), background);

    final line = Paint()
      ..color = Colors.white
      ..style = PaintingStyle.stroke
      ..strokeWidth = size.width * 0.075
      ..strokeCap = StrokeCap.round
      ..strokeJoin = StrokeJoin.round;
    final house = Path()
      ..moveTo(size.width * 0.2, size.height * 0.5)
      ..lineTo(size.width * 0.5, size.height * 0.23)
      ..lineTo(size.width * 0.8, size.height * 0.5)
      ..lineTo(size.width * 0.8, size.height * 0.78)
      ..lineTo(size.width * 0.2, size.height * 0.78)
      ..close();
    canvas.drawPath(house, line);
    canvas.drawCircle(
      Offset(size.width * 0.5, size.height * 0.55),
      size.width * 0.105,
      Paint()..color = Colors.white,
    );
    canvas.drawLine(
      Offset(size.width * 0.5, size.height * 0.55),
      Offset(size.width * 0.68, size.height * 0.69),
      line..strokeWidth = size.width * 0.055,
    );
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) => false;
}
